/* File: portal.c
 * ------------
 * Web Portal 认证后端（Dr.COM 哆点门户 4.x）。
 *
 * 关键设计（实测结论）：
 *   1. HTTP 绑定实例的源 IPv4（每次请求前用 SIOCGIFADDR 重新获取），
 *      而不是无线设备名——apcli0 等无线 netdev 设备绑定会间歇性失败，
 *      源 IP 绑定连续稳定。无 IPv4 时不回退默认路由，等待接口恢复。
 *   2. 客户端 MAC 使用实例 netdev 的真实 MAC，不信任服务器回显。
 *   3. 登录后端按 portal_login_mode 选择：AUTO 默认 Dr.COM Web
 *      （/drcom/login，实测可用），ePortal 仅在显式选择时使用。
 *   4. 登录成功后必须二次 chkstatus 确认才标记 online。
 *
 * Location 是运行时事实来源：启动时经 PortalLocationParse() 完整解析，
 * 全部 API endpoint 由 portal_endpoint.c 按 Location origin / scheme
 * 动态生成——源码不包含任何真实学校 Portal 地址、端口或查询串。
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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#define PORTAL_BODY_MAX 4096
#define PORTAL_VALUE_MAX 128

/* 运行时上下文：Location / 网络 / 后端选择 / 请求状态 */
typedef struct {
	PortalLocationInfo location;
	PortalConfig cfg;
	PortalDrcomLoginConfig drcom;

	char source_ipv4[64];
	char local_mac[32];

	unsigned int request_seq;
	unsigned int transport_failures;
	unsigned int auth_failures;
} PortalRuntimeContext;

typedef struct {
	int online;
	char server_ip[64];
	char uid[128];
	char server_mac[32];
	char result[16];
} PortalStatusInfo;

typedef struct {
	long http_code;
	char body[PORTAL_BODY_MAX];
	size_t body_len;
	char curl_reason[32];
} PortalHttpResponse;

static PortalRuntimeContext PortalCtx;
static int curl_ready = 0;

/* ---------------- HTTP 客户端 ---------------- */

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

/* 当前实例 netdev 的源 IPv4（每次请求前实时获取） */
static int portal_local_ipv4(const char *ifname, char *out, size_t outlen) {
	struct ifreq ifr;
	struct sockaddr_in *sin;
	int fd;

	if (!ifname || !ifname[0])
		return -1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

	if (ioctl(fd, SIOCGIFADDR, &ifr) != 0) {
		close(fd);
		return -1;
	}
	close(fd);

	sin = (struct sockaddr_in *) &ifr.ifr_addr;
	if (!inet_ntop(AF_INET, &sin->sin_addr, out, outlen))
		return -1;

	return 0;
}

/* 实例 netdev 的真实 MAC（12 位无分隔），不使用服务器回显值 */
static int portal_local_mac(const char *ifname, char *out, size_t outlen) {
	char path[128];
	char addr[64];
	FILE *f;
	size_t i, n = 0;

	if (!ifname || !ifname[0])
		return -1;

	snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifname);
	f = fopen(path, "r");
	if (!f)
		return -1;

	if (!fgets(addr, sizeof(addr), f)) {
		fclose(f);
		return -1;
	}
	fclose(f);

	for (i = 0; addr[i] && n + 1 < outlen; i++) {
		if (addr[i] != ':' && addr[i] != '\n')
			out[n++] = addr[i];
	}
	out[n] = 0;

	return (n == 12) ? 0 : -1;
}

/* 刷新本请求的网络身份；失败（无 IPv4）时禁止回退默认路由 */
static int portal_refresh_network(void) {
	char ipv4[64];
	char mac[32];

	ipv4[0] = 0;
	mac[0] = 0;

	if (portal_local_ipv4(DeviceName, ipv4, sizeof(ipv4)) != 0) {
		PortalCtx.source_ipv4[0] = 0;
		RuntimeStatusSet("waiting_interface", "portal_no_ipv4");
		LogWrite(DRCOM, WARN, "Portal: %s has no IPv4, waiting.",
				DeviceName);
		return -1;
	}

	portal_local_mac(DeviceName, mac, sizeof(mac));

	snprintf(PortalCtx.source_ipv4, sizeof(PortalCtx.source_ipv4),
			"%s", ipv4);
	snprintf(PortalCtx.local_mac, sizeof(PortalCtx.local_mac),
			"%s", mac);
	return 0;
}

/*
 * 接口绑定的 HTTP GET（源地址 = 当前实例 IPv4）。
 * 返回 0 表示传输层成功；协议层结果由调用方按 body 判断。
 */
static int PortalHttpGet(const char *action, const char *url,
		PortalHttpResponse *response) {
	struct portal_mem mem;
	CURL *curl;
	char errbuf[CURL_ERROR_SIZE];
	char primary_ip[64] = "";
	char primary_port[16] = "";
	CURLcode rc;

	portal_curl_init();

	memset(response, 0, sizeof(*response));

	if (!PortalCtx.source_ipv4[0]) {
		snprintf(response->curl_reason, sizeof(response->curl_reason),
				"portal_no_ipv4");
		return -1;
	}

	curl = curl_easy_init();
	if (!curl) {
		snprintf(response->curl_reason, sizeof(response->curl_reason),
				"portal_http_failed");
		return -1;
	}

	mem.buf = response->body;
	mem.len = 0;
	response->body[0] = 0;
	errbuf[0] = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_INTERFACE, PortalCtx.source_ipv4);
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
		LogWrite(DRCOM, ERROR,
				"Portal HTTP failed: action=%s device=%s source=%s reason=%s detail=%s",
				action, DeviceName, PortalCtx.source_ipv4,
				response->curl_reason,
				errbuf[0] ? errbuf : curl_easy_strerror(rc));
		curl_easy_cleanup(curl);
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->http_code);
	{
		char *s = NULL;
		if (curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &s) == CURLE_OK && s)
			snprintf(primary_ip, sizeof(primary_ip), "%s", s);
		if (curl_easy_getinfo(curl, CURLINFO_PRIMARY_PORT, &s) == CURLE_OK && s)
			snprintf(primary_port, sizeof(primary_port), "%s", s);
	}

	LogWrite(DRCOM, INF,
			"Portal HTTP: action=%s device=%s source=%s remote=%s:%s http=%ld",
			action, DeviceName, PortalCtx.source_ipv4,
			primary_ip, primary_port, response->http_code);

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

/* chkstatus：服务器状态与本地身份分离存放 */
static int PortalCheckStatus(PortalStatusInfo *st) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	PortalHttpResponse response;

	memset(st, 0, sizeof(*st));

	if (PortalBuildKernelStatusURL(&PortalCtx.location, url, sizeof(url)) != 0)
		return -1;

	if (PortalHttpGet("chkstatus", url, &response) != 0) {
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

	json_get(json, "result", st->result, sizeof(st->result));
	st->online = (strcmp(st->result, "1") == 0);

	json_get(json, "uid", st->uid, sizeof(st->uid));

	/* 服务器视角 IP（与本地源 IPv4 仅作交叉检查，不覆盖绑定） */
	if (json_get(json, "ss5", st->server_ip, sizeof(st->server_ip)) != 0 ||
			!st->server_ip[0])
		json_get(json, "v46ip", st->server_ip, sizeof(st->server_ip));

	/* ss4 非全零才是服务器返回的客户端 MAC；本地 MAC 另行获取 */
	json_get(json, "ss4", st->server_mac, sizeof(st->server_mac));
	if (!st->server_mac[0] || !strcmp(st->server_mac, "000000000000"))
		json_get(json, "ss1", st->server_mac, sizeof(st->server_mac));

	if (st->server_ip[0] && PortalCtx.source_ipv4[0] &&
			strcmp(st->server_ip, PortalCtx.source_ipv4) != 0)
		LogWrite(DRCOM, WARN,
				"Portal: server-view IP %s differs from local source %s.",
				st->server_ip, PortalCtx.source_ipv4);

	return 0;
}

/* 账号 + 可选运营商后缀（账号已含 @ 不重复追加） */
static void portal_build_account(char *out, size_t outlen) {
	if (PortalSuffix && PortalSuffix[0] &&
			UserName && UserName[0] && !strchr(UserName, '@')) {
		snprintf(out, outlen, "%s%s", UserName, PortalSuffix);
	} else {
		snprintf(out, outlen, "%s", UserName ? UserName : "");
	}
}

/* Dr.COM Web 登录（/drcom/login，实测可用路径） */
static int portal_login_drcom(char *msg, size_t msglen) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char account[PORTAL_ENC_MAX];
	char callback[16];
	char result[16];
	PortalHttpResponse response;

	snprintf(callback, sizeof(callback), "dr%u",
			++PortalCtx.request_seq);

	portal_build_account(account, sizeof(account));

	if (PortalBuildDrcomLoginURL(&PortalCtx.location, &PortalCtx.drcom,
			callback, 500 + (PortalCtx.request_seq * 37u) % 10000u,
			account, Password ? Password : "",
			url, sizeof(url)) != 0)
		return -1;

	if (PortalHttpGet("login(drcom)", url, &response) != 0) {
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

	{
		char msga[PORTAL_VALUE_MAX];
		char detail[PORTAL_VALUE_MAX];

		msg[0] = 0;
		msga[0] = 0;
		json_get(json, "msg", msg, msglen);
		json_get(json, "msga", msga, sizeof(msga));

		if (msga[0])
			snprintf(detail, sizeof(detail), "%s", msga);
		else if (msg[0])
			snprintf(detail, sizeof(detail), "%s", msg);
		else
			snprintf(detail, sizeof(detail), "login_failed");

		LogWrite(DRCOM, ERROR,
				"Portal login failed: backend=drcom result=%s msg=%s detail=%s",
				result[0] ? result : "?", msg, detail);

		/* 认证失败 detail 使用服务器可读原因 */
		snprintf(msg, msglen, "%s", detail);
	}

	return -1;
}

/* ePortal 登录（显式选择 portal_protocol=eportal 时使用） */
static int portal_login_eportal(char *msg, size_t msglen) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char account[PORTAL_ENC_MAX];
	char result[16];
	PortalHttpResponse response;

	portal_build_account(account, sizeof(account));

	if (PortalBuildEportalLoginURL(&PortalCtx.location, &PortalCtx.cfg,
			account, Password ? Password : "",
			PortalCtx.source_ipv4, PortalCtx.local_mac,
			url, sizeof(url)) != 0)
		return -1;

	if (PortalHttpGet("login(eportal)", url, &response) != 0) {
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

/* 后端选择 + 登录成功后的二次 chkstatus 确认
 * 返回 0 = 已确认在线；1 = 需要重试（reconnecting）；-1 = 认证失败 */
static int portal_login_attempt(char *msg, size_t msglen) {
	int rc;
	PortalStatusInfo st;

	if (PortalLoginBackend == PORTAL_LOGIN_EPORTAL)
		rc = portal_login_eportal(msg, msglen);
	else
		rc = portal_login_drcom(msg, msglen);

	if (rc != 0)
		return -1;

	/* 登录成功 != online：二次 chkstatus 确认 */
	sleep(1);

	if (PortalCheckStatus(&st) != 0)
		return 1;

	if (!st.online) {
		snprintf(msg, msglen, "verify_pending");
		return 1;
	}

	return 0;
}

/* ---------------- 对外接口 ---------------- */

void PortalLogout(void) {
	char url[PORTAL_URL_MAX];
	char json[PORTAL_BODY_MAX];
	char result[16];
	PortalLocationInfo logout_loc;
	PortalHttpResponse response;

	if (PortalLocationParse(PortalLocation, &logout_loc) != 0) {
		LogWrite(DRCOM, ERROR, "Portal: invalid Location for logout.");
		return;
	}

	portal_curl_init();

	if (portal_refresh_network() != 0) {
		LogWrite(DRCOM, WARN,
				"Portal: no IPv4 on %s, skip logout.",
				DeviceName);
		return;
	}

	if (PortalBuildKernelLogoutURL(&logout_loc, url, sizeof(url)) != 0)
		return;

	LogWrite(DRCOM, INF, "Portal: send logout.");

	result[0] = 0;

	if (PortalHttpGet("logout", url, &response) == 0 &&
			json_extract(response.body, json, sizeof(json)) == 0)
		json_get(json, "result", result, sizeof(result));

	if (strcmp(result, "1") == 0)
		LogWrite(DRCOM, INF, "Portal: logged off.");
	else
		LogWrite(DRCOM, WARN, "Portal: logout not confirmed.");
}

void PortalRun(void) {
	char msg[64];
	unsigned int delay;

	if (PortalLocationParse(PortalLocation, &PortalCtx.location) != 0) {
		RuntimeStatusSet("error", "portal_location_invalid");
		LogWrite(DRCOM, ERROR,
				"Portal: invalid Location (scheme must be http/https, "
				"host required, no userinfo)!");
		exit(EXIT_FAILURE);
	}

	PortalConfigDefaults(&PortalCtx.cfg);
	if (PortalEportalHttpPort > 0)
		PortalCtx.cfg.eportal_http_port =
				(unsigned int) PortalEportalHttpPort;
	if (PortalEportalHttpsPort > 0)
		PortalCtx.cfg.eportal_https_port =
				(unsigned int) PortalEportalHttpsPort;

	PortalDrcomLoginDefaults(&PortalCtx.drcom);
	if (PortalProgramIndex && PortalProgramIndex[0])
		snprintf(PortalCtx.drcom.program_index,
				sizeof(PortalCtx.drcom.program_index),
				"%s", PortalProgramIndex);
	if (PortalPageIndex > 0)
		PortalCtx.drcom.page_index = PortalPageIndex;
	if (PortalJsVersion && PortalJsVersion[0])
		snprintf(PortalCtx.drcom.js_version,
				sizeof(PortalCtx.drcom.js_version),
				"%s", PortalJsVersion);
	if (PortalR3 && PortalR3[0])
		snprintf(PortalCtx.drcom.r3, sizeof(PortalCtx.drcom.r3),
				"%s", PortalR3);

	portal_curl_init();

	LogWrite(DRCOM, INF,
			"Portal: Location parsed, scheme=%s host=%s port=%u "
			"path=%s device=%s backend=%s.",
			PortalCtx.location.scheme, PortalCtx.location.host,
			PortalCtx.location.port, PortalCtx.location.path,
			DeviceName,
			PortalLoginBackend == PORTAL_LOGIN_EPORTAL ?
			"eportal" : "drcom");

	for (;;) {
		PortalStatusInfo st;
		int login_rc;

		msg[0] = 0;

		if (portal_refresh_network() != 0) {
			sleep(PortalCheckInterval);
			continue;
		}

		if (PortalCheckStatus(&st) != 0) {
			PortalCtx.transport_failures++;
			delay = PortalCheckInterval *
					(1 << (PortalCtx.transport_failures > 4 ?
					4 : PortalCtx.transport_failures));
			sleep(delay);
			continue;
		}

		if (st.online) {
			RuntimeStatusSet("online", "portal_online");
			RuntimeStatusHeartbeat();
			PortalCtx.transport_failures = 0;
			PortalCtx.auth_failures = 0;
			sleep(PortalCheckInterval);
			continue;
		}

		RuntimeStatusSet("authenticating", "portal_login");

		login_rc = portal_login_attempt(msg, sizeof(msg));

		if (login_rc == 0) {
			RuntimeStatusSet("online", "portal_login_ok");
			RuntimeStatusHeartbeat();
			LogWrite(DRCOM, INF, "Portal: login verified via chkstatus.");
			PortalCtx.transport_failures = 0;
			PortalCtx.auth_failures = 0;
			sleep(PortalCheckInterval);
		} else if (login_rc == 1) {
			/* 传输/确认类失败：指数退避 */
			RuntimeStatusSet("reconnecting",
					msg[0] ? msg : "reconnecting");
			PortalCtx.transport_failures++;
			delay = PortalCheckInterval *
					(1 << (PortalCtx.transport_failures > 4 ?
					4 : PortalCtx.transport_failures));
			LogWrite(DRCOM, WARN,
					"Portal: retry in %us (%s).", delay, msg);
			sleep(delay);
		} else {
			/* 账号/协议失败：慢速重试（30s），等用户改配置 */
			RuntimeStatusSet("auth_failed", msg);
			PortalCtx.auth_failures++;
			LogWrite(DRCOM, ERROR,
					"Portal: auth failed (%s), retry in 30s.",
					msg);
			sleep(30);
		}
	}
}
