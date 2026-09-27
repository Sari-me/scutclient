/* File: portal.c
 * ------------
 * Web Portal 认证后端（Dr.COM 哆点门户 4.x，eportal/Radius 方式）。
 *
 * Location 是运行时事实来源：启动时经 PortalLocationParse() 完整解析，
 * 全部 API endpoint 由 portal_endpoint.c 按 Location origin / scheme
 * 动态生成——源码不包含任何真实学校 Portal 地址、端口或查询串。
 *
 * 协议（登录页 a41.js/a40.js + pscut/weblogin.py 实测）：
 *   全部接口为 HTTP GET，响应为 callback(JSON) 或纯 JSON；
 *   - 状态:  <origin>/drcom/chkstatus?callback=dr&jsVersion=4.1.3&lang=zh
 *   - 登录:  <origin>:<eportal-port>/eportal/portal/login?callback=dr&...
 *   - 注销:  <origin>/drcom/logout?callback=dr&jsVersion=4.1.3&lang=zh
 *
 * 服务器按 TCP 来源 IP 识别终端：所有请求用 CURLOPT_INTERFACE 绑定
 * 实例 netdev，wlan_user_ip / wlan_user_mac 取 chkstatus 的服务器视角值。
 * result:1 = 成功；result:0 = 失败（msg 为原因，可能是 GBK 编码）。
 */
#include "portal.h"
#include "info.h"
#include "tracelog.h"
#include "runtime_status.h"
#include "portal_endpoint.h"
#include "portal_url.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define PORTAL_BODY_MAX 4096
#define PORTAL_VALUE_MAX 128

static PortalLocationInfo PortalEndpoint;
static PortalConfig PortalCfg;
static int curl_ready = 0;

/* ---------------- HTTP 客户端 ---------------- */

typedef struct {
	long http_code;
	char body[PORTAL_BODY_MAX];
	size_t body_len;
	char effective_url[PORTAL_URL_MAX];
	char curl_reason[32];
} PortalHttpResponse;

struct portal_mem {
	char *buf;
	size_t len;
};

static size_t portal_write_cb(void *data, size_t size, size_t nmemb, void *userp) {
	struct portal_mem *mem = (struct portal_mem *) userp;
	size_t total = size * nmemb;

	if (total && mem->len + total >= PORTAL_BODY_MAX)
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

/* 传输层错误分类（写入 runtime status detail） */
static const char *portal_curl_reason(CURLcode rc) {
	switch (rc) {
	case CURLE_COULDNT_RESOLVE_HOST:
		return "portal_dns_failed";
	case CURLE_COULDNT_CONNECT:
	case CURLE_OPERATION_TIMEDOUT:
		return "portal_connect_failed";
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_PEER_FAILED_VERIFICATION:
		return "portal_tls_failed";
	default:
		return "portal_http_failed";
	}
}

/*
 * 接口绑定的 HTTP GET。返回 0 表示传输层成功（HTTP code 记录在
 * response->http_code，协议层结果由调用方按 body 判断）。
 */
static int PortalHttpGet(const char *url, PortalHttpResponse *response) {
	struct portal_mem mem;
	CURL *curl;
	char errbuf[CURL_ERROR_SIZE];
	CURLcode rc;

	portal_curl_init();

	memset(response, 0, sizeof(*response));

	curl = curl_easy_init();
	if (!curl) {
		response->http_code = 0;
		snprintf(response->body, sizeof(response->body),
				"curl init failed");
		return -1;
	}

	mem.buf = response->body;
	mem.len = 0;
	response->body[0] = 0;
	errbuf[0] = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_INTERFACE, DeviceName);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
			(long) PortalConnectTimeout * 1000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
			(long) PortalTimeout * 1000L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, portal_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

	/* TLS 跟随实际请求 URL：https 默认严格校验，portal_tls_verify=0 才关闭 */
	if (strncmp(url, "https://", 8) == 0) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
				PortalTlsVerify ? 1L : 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,
				PortalTlsVerify ? 2L : 0L);
	}

	rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		snprintf(response->curl_reason, sizeof(response->curl_reason),
				"%s", portal_curl_reason(rc));
		LogWrite(DRCOM, ERROR, "Portal: request failed (%s): %s",
				response->curl_reason,
				errbuf[0] ? errbuf : curl_easy_strerror(rc));
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->http_code);

	{
		char *effective = NULL;
		if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL,
				&effective) == CURLE_OK && effective) {
			snprintf(response->effective_url,
					sizeof(response->effective_url), "%s", effective);
		}
	}

	curl_easy_cleanup(curl);
	return 0;
}

/* ---------------- JSON / JSONP 最小解析 ---------------- */

/* 响应可能是纯 JSON（首个非空白字符 '{'）或 callback(JSON) 包装 */
static int json_extract(const char *body, char *out, size_t outlen) {
	const char *p = body;
	const char *e;

	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;

	if (*p == '{') {
		e = body + strlen(body);
		while (e > p && (e[-1] == '\n' || e[-1] == '\r' ||
				e[-1] == ' ' || e[-1] == ';'))
			e--;
		if ((size_t) (e - p) >= outlen)
			return -1;
		memcpy(out, p, (size_t) (e - p));
		out[e - p] = 0;
		return 0;
	}

	p = strchr(body, '(');
	if (!p)
		return -1;

	e = strrchr(p, ')');
	if (!e || e <= p)
		return -1;

	p++;
	if ((size_t) (e - p) >= outlen)
		return -1;

	memcpy(out, p, (size_t) (e - p));
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

/* ---------------- 协议动作 ---------------- */

/* result:1 在线；离线时返回服务器视角 IP/MAC 供登录使用 */
static int portal_chkstatus(int *online, char *ip, size_t iplen,
		char *mac, size_t maclen) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char result[16];
	PortalHttpResponse response;

	if (PortalBuildKernelStatusURL(&PortalEndpoint, url, sizeof(url)) != 0)
		return -1;

	if (PortalHttpGet(url, &response) != 0) {
		RuntimeStatusSet("reconnecting",
				response.curl_reason[0] ?
				response.curl_reason : "portal_request_failed");
		return -1;
	}

	if (json_extract(response.body, json, sizeof(json)) != 0) {
		RuntimeStatusSet("reconnecting", "portal_bad_response");
		LogWrite(DRCOM, ERROR,
				"Portal: chkstatus unexpected response (HTTP %ld).",
				response.http_code);
		return -1;
	}

	result[0] = 0;
	json_get(json, "result", result, sizeof(result));
	*online = (strcmp(result, "1") == 0);

	if (json_get(json, "ss5", ip, iplen) != 0 || !ip[0])
		json_get(json, "v46ip", ip, iplen);

	if (json_get(json, "ss1", mac, maclen) != 0)
		mac[0] = 0;

	LogWrite(DRCOM, INF,
			"Portal: chkstatus %s://%s:%u via %s (HTTP %ld).",
			PortalEndpoint.scheme, PortalEndpoint.host,
			PortalEndpoint.port, DeviceName, response.http_code);

	return 0;
}

static int portal_login(const char *ip, const char *mac_raw, char *msg,
		size_t msglen) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char account[PORTAL_ENC_MAX];
	char mac[32];
	char result[16];
	size_t i, n = 0;
	PortalHttpResponse response;

	/* 12 位无分隔 MAC */
	for (i = 0; mac_raw && mac_raw[i] && n + 1 < sizeof(mac); i++) {
		if (mac_raw[i] != ':' && mac_raw[i] != '-')
			mac[n++] = mac_raw[i];
	}
	mac[n] = 0;
	if (!n)
		strcpy(mac, "000000000000");

	/* 账号 + 运营商后缀（可选；账号已含 @ 时不重复追加） */
	if (PortalSuffix && PortalSuffix[0] &&
			UserName && UserName[0] && !strchr(UserName, '@')) {
		snprintf(account, sizeof(account), "%s%s", UserName, PortalSuffix);
	} else {
		snprintf(account, sizeof(account), "%s",
				UserName ? UserName : "");
	}

	if (PortalBuildEportalLoginURL(&PortalEndpoint, &PortalCfg,
			account, Password ? Password : "",
			ip, mac, url, sizeof(url)) != 0)
		return -1;

	if (PortalHttpGet(url, &response) != 0) {
		snprintf(msg, msglen, "%s",
				response.curl_reason[0] ?
				response.curl_reason : "request_failed");
		return -1;
	}

	if (json_extract(response.body, json, sizeof(json)) != 0) {
		snprintf(msg, msglen, "bad_response");
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
	PortalHttpResponse response;

	if (PortalLocationParse(PortalLocation, &PortalEndpoint) != 0) {
		LogWrite(DRCOM, ERROR, "Portal: invalid Location for logout.");
		return;
	}

	portal_curl_init();

	if (PortalBuildKernelLogoutURL(&PortalEndpoint, url, sizeof(url)) != 0)
		return;

	LogWrite(DRCOM, INF, "Portal: send logout.");

	result[0] = 0;

	if (PortalHttpGet(url, &response) == 0 &&
			json_extract(response.body, json, sizeof(json)) == 0)
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

	if (PortalLocationParse(PortalLocation, &PortalEndpoint) != 0) {
		RuntimeStatusSet("error", "portal_location_invalid");
		LogWrite(DRCOM, ERROR,
				"Portal: invalid Location (scheme must be http/https, "
				"host required, no userinfo)!");
		exit(EXIT_FAILURE);
	}

	PortalConfigDefaults(&PortalCfg);
	if (PortalEportalHttpPort > 0)
		PortalCfg.eportal_http_port = (unsigned int) PortalEportalHttpPort;
	if (PortalEportalHttpsPort > 0)
		PortalCfg.eportal_https_port = (unsigned int) PortalEportalHttpsPort;

	portal_curl_init();

	/* 启动日志：只记录 endpoint 概要，不输出 Location query */
	LogWrite(DRCOM, INF,
			"Portal: Location parsed, scheme=%s host=%s port=%u "
			"path=%s device=%s.",
			PortalEndpoint.scheme, PortalEndpoint.host,
			PortalEndpoint.port, PortalEndpoint.path, DeviceName);

	for (;;) {
		int online = 0;

		ip[0] = 0;
		mac[0] = 0;

		if (portal_chkstatus(&online, ip, sizeof(ip),
				mac, sizeof(mac)) != 0) {
			failures++;
		} else if (online) {
			RuntimeStatusSet("online", "portal_online");
			RuntimeStatusHeartbeat();
			LogWrite(DRCOM, INF, "Portal: online (uid check via chkstatus).");
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

		/* 连续失败时按检测间隔指数退避（1x..16x），上限 16 倍 */
		sleep(PortalCheckInterval *
				(1 << (failures > 4 ? 4 : failures)));
	}
}
