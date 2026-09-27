/* File: portal.c
 * ------------
 * Web Portal 认证后端（Dr.COM 哆点门户 4.x，eportal/Radius 方式）。
 *
 * 协议（登录页 a41.js/a40.js + pscut/weblogin.py 对 202.38.210.132 实测）：
 *   全部接口为 HTTP GET + 查询参数，响应为 callback(JSON)，无加密、无 Cookie；
 *   - 状态:  GET http://<host>/drcom/chkstatus?callback=dr&jsVersion=4.1.3&lang=zh
 *   - 登录:  GET http://<host>:801/eportal/portal/login?callback=dr&login_method=1
 *            &user_account=..&user_password=..&wlan_user_ip=..&wlan_user_mac=..
 *            &terminal_type=1&jsVersion=4.1.3&lang=zh
 *   - 注销:  GET http://<host>/drcom/logout?callback=dr&jsVersion=4.1.3&lang=zh
 *
 * 服务器按 TCP 来源 IP 识别终端：所有请求用 CURLOPT_INTERFACE 绑定实例
 * netdev，wlan_user_ip / wlan_user_mac 取 chkstatus 返回的服务器视角值。
 * result:1 = 成功；result:0 = 失败（msg 为原因，可能是 GBK 编码）。
 */
#include "portal.h"
#include "info.h"
#include "tracelog.h"
#include "runtime_status.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define PORTAL_KERNEL_PORT  80
#define PORTAL_EPORTAL_PORT 801
#define PORTAL_JS_VERSION   "4.1.3"

#define PORTAL_HOST_MAX  128
#define PORTAL_URL_MAX   4096
#define PORTAL_ENC_MAX   1536
#define PORTAL_BODY_MAX  4096
#define PORTAL_VALUE_MAX 128

static char portal_host[PORTAL_HOST_MAX] = "";
static int  curl_ready = 0;

/* ---------------- JSON / JSONP 最小解析 ---------------- */

/* 去掉 callback(...) 包装，取出 JSON 文本 */
static int jsonp_extract(const char *body, char *out, size_t outlen) {
	const char *p = strchr(body, '(');
	const char *e;

	if (!p)
		return -1;

	e = strrchr(p, ')');
	if (!e || e <= p)
		return -1;

	p++;
	if ((size_t)(e - p) >= outlen)
		return -1;

	memcpy(out, p, (size_t)(e - p));
	out[e - p] = 0;
	return 0;
}

/* 取顶层字符串/数值字段（本协议取值足够；不支持嵌套） */
static int json_get(const char *json, const char *key,
		char *out, size_t outlen) {
	char pat[64];
	const char *p;
	size_t n = 0;

	snprintf(pat, sizeof(pat), "\"%s\"", key);
	p = strstr(json, pat);
	if (!p)
		return -1;

	p += strlen(pat);
	while (*p == ' ' || *p == ':')
		p++;

	if (*p == '"') {
		p++;
		while (*p && *p != '"') {
			if (*p == '\\' && p[1])
				p++;
			if (n + 1 < outlen)
				out[n++] = *p;
			p++;
		}
	} else {
		while (*p && *p != ',' && *p != '}' && *p != ' ') {
			if (n + 1 < outlen)
				out[n++] = *p;
			p++;
		}
	}

	out[n] = 0;
	return 0;
}

/* ---------------- URL 构造 ---------------- */

static void url_encode(const char *in, char *out, size_t outlen) {
	static const char hex[] = "0123456789ABCDEF";
	size_t n = 0;

	for (; *in && n + 4 <= outlen; in++) {
		unsigned char c = (unsigned char)*in;

		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~') {
			out[n++] = (char)c;
		} else {
			out[n++] = '%';
			out[n++] = hex[c >> 4];
			out[n++] = hex[c & 0x0F];
		}
	}

	out[n] = 0;
}

/* 从 Location 提取主机名（去掉可能携带的端口与路径） */
static int portal_parse_host(const char *location) {
	const char *p;
	const char *end;
	size_t n;

	if (!location)
		return -1;

	p = strstr(location, "://");
	if (!p)
		return -1;

	p += 3;
	end = p;
	while (*end && *end != '/' && *end != ':' && *end != '?')
		end++;

	n = (size_t)(end - p);
	if (n == 0 || n >= sizeof(portal_host))
		return -1;

	memcpy(portal_host, p, n);
	portal_host[n] = 0;
	return 0;
}

/* ---------------- libcurl GET ---------------- */

struct portal_mem {
	char *buf;
	size_t len;
};

static size_t portal_write_cb(void *data, size_t size, size_t nmemb, void *userp) {
	struct portal_mem *mem = (struct portal_mem *) userp;
	size_t total = size * nmemb;

	if (mem->len + total >= PORTAL_BODY_MAX)
		total = PORTAL_BODY_MAX - 1 - mem->len;

	memcpy(mem->buf + mem->len, data, total);
	mem->len += total;
	mem->buf[mem->len] = 0;
	return size * nmemb;
}

static void portal_curl_init(void) {
	if (!curl_ready) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		curl_ready = 1;
	}
}

/* GET 一个 JSONP 接口，返回 0 表示 HTTP 请求成功且拿到 JSON */
static int portal_get(const char *url, char *json, size_t jsonlen) {
	struct portal_mem mem;
	CURL *curl;
	char body[PORTAL_BODY_MAX];
	char errbuf[CURL_ERROR_SIZE];

	portal_curl_init();

	curl = curl_easy_init();
	if (!curl)
		return -1;

	memset(&mem, 0, sizeof(mem));
	mem.buf = body;
	body[0] = 0;
	errbuf[0] = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_INTERFACE, DeviceName);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
			(long)PortalConnectTimeout * 1000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
			(long)PortalTimeout * 1000L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, portal_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

	if (portal_tls && strncmp(url, "https://", 8) == 0)
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
				PortalTlsVerify ? 1L : 0L);

	if (curl_easy_perform(curl) != CURLE_OK) {
		LogWrite(DRCOM, ERROR, "Portal: request failed: %s",
				errbuf[0] ? errbuf : "unknown error");
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_cleanup(curl);

	if (jsonp_extract(body, json, jsonlen) != 0) {
		LogWrite(DRCOM, ERROR, "Portal: unexpected response format.");
		return -1;
	}

	return 0;
}

/* ---------------- 协议动作 ---------------- */

/* result:1 在线；离线时返回服务器视角 IP/MAC 供登录使用 */
static int portal_chkstatus(int *online, char *ip, size_t iplen,
		char *mac, size_t maclen) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char result[16];

	snprintf(url, sizeof(url),
			"http://%s:%d/drcom/chkstatus?callback=dr&jsVersion="
			PORTAL_JS_VERSION "&lang=zh",
			portal_host, PORTAL_KERNEL_PORT);

	if (portal_get(url, json, sizeof(json)) != 0)
		return -1;

	result[0] = 0;
	json_get(json, "result", result, sizeof(result));
	*online = (strcmp(result, "1") == 0);

	if (json_get(json, "ss5", ip, iplen) != 0 || !ip[0])
		json_get(json, "v46ip", ip, iplen);

	if (json_get(json, "ss1", mac, maclen) != 0)
		mac[0] = 0;

	return 0;
}

static int portal_login(const char *ip, const char *mac_raw, char *msg,
		size_t msglen) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char enc_account[PORTAL_ENC_MAX];
	char enc_password[PORTAL_ENC_MAX];
	char account[PORTAL_ENC_MAX];
	char mac[32];
	char result[16];
	size_t i, n;

	/* 12 位无分隔 MAC */
	for (i = 0, n = 0; mac_raw && mac_raw[i] && n + 1 < sizeof(mac); i++) {
		if (mac_raw[i] != ':' && mac_raw[i] != '-')
			mac[n++] = mac_raw[i];
	}
	mac[n] = 0;
	if (!n)
		strcpy(mac, "000000000000");

	/* 账号 + 运营商后缀（portal_suffix 原样追加，如 @dx / @lt） */
	snprintf(account, sizeof(account), "%s%s",
			UserName ? UserName : "",
			PortalSuffix ? PortalSuffix : "");

	url_encode(account, enc_account, sizeof(enc_account));
	url_encode(Password ? Password : "", enc_password, sizeof(enc_password));

	snprintf(url, sizeof(url),
			"http://%s:%d/eportal/portal/login?callback=dr&login_method=1"
			"&user_account=%s&user_password=%s"
			"&wlan_user_ip=%s&wlan_user_ipv6=&wlan_user_mac=%s"
			"&wlan_ac_ip=&wlan_ac_name=&terminal_type=1"
			"&jsVersion=" PORTAL_JS_VERSION "&lang=zh",
			portal_host, PORTAL_EPORTAL_PORT,
			enc_account, enc_password, ip, mac);

	if (portal_get(url, json, sizeof(json)) != 0) {
		snprintf(msg, msglen, "request_failed");
		return -1;
	}

	result[0] = 0;
	json_get(json, "result", result, sizeof(result));

	if (strcmp(result, "1") == 0)
		return 0;

	if (json_get(json, "msg", msg, msglen) != 0 || !msg[0])
		snprintf(msg, msglen, "login_failed");

	return -1;
}

/* ---------------- 对外接口 ---------------- */

void PortalLogout(void) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char result[16];

	if (!portal_host[0] &&
			portal_parse_host(PortalLocation) != 0) {
		LogWrite(DRCOM, ERROR, "Portal: invalid Location for logout.");
		return;
	}

	portal_curl_init();

	snprintf(url, sizeof(url),
			"http://%s:%d/drcom/logout?callback=dr&jsVersion="
			PORTAL_JS_VERSION "&lang=zh",
			portal_host, PORTAL_KERNEL_PORT);

	LogWrite(DRCOM, INF, "Portal: send logout.");

	result[0] = 0;

	if (portal_get(url, json, sizeof(json)) == 0)
		json_get(json, "result", result, sizeof(result));

	if (strcmp(result, "1") == 0)
		LogWrite(DRCOM, INF, "Portal: logged off.");
	else
		LogWrite(DRCOM, WARN, "Portal: logout not confirmed.");
}

void PortalRun(void) {
	char ip[PORTAL_VALUE_MAX] = "";
	char mac[PORTAL_VALUE_MAX] = "";
	char msg[64];
	char logmsg[PORTAL_VALUE_MAX];
	unsigned int failures = 0;

	if (portal_parse_host(PortalLocation) != 0) {
		RuntimeStatusSet("error", "portal_location_invalid");
		LogWrite(DRCOM, ERROR, "Portal: cannot parse Location host!");
		exit(EXIT_FAILURE);
	}

	portal_curl_init();

	LogWrite(DRCOM, INF, "Portal: authentication loop started (host %s).",
			portal_host);

	for (;;) {
		int online = 0;

		ip[0] = 0;
		mac[0] = 0;

		if (portal_chkstatus(&online, ip, sizeof(ip),
				mac, sizeof(mac)) != 0) {
			RuntimeStatusSet("reconnecting", "chkstatus_failed");
			failures++;
		} else if (online) {
			RuntimeStatusSet("online", "portal_online");
			RuntimeStatusHeartbeat();
			failures = 0;
		} else {
			RuntimeStatusSet("authenticating", "portal_login");

			msg[0] = 0;
			if (portal_login(ip, mac, logmsg, sizeof(logmsg)) == 0) {
				RuntimeStatusSet("online", "portal_login_ok");
				RuntimeStatusHeartbeat();
				LogWrite(DRCOM, INF, "Portal: login success.");
				failures = 0;
			} else {
				snprintf(msg, sizeof(msg), "%s", logmsg);
				RuntimeStatusSet("auth_failed", msg);
				LogWrite(DRCOM, ERROR, "Portal: login failed: %s", msg);
				failures++;
			}
		}

		/* 连续失败时按 2 的幂退避（1x..16x 检测间隔），上限 16 倍 */
		sleep(PortalCheckInterval *
				(1 << (failures > 4 ? 4 : failures)));
	}
}
