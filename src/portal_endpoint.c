/* File: portal_endpoint.c
 * ------------
 * Portal API endpoint 构造：
 *   kernel 接口（chkstatus/logout）跟随 Location origin
 *   （same scheme + host + 有效端口）；
 *   ePortal 登录按 Location scheme 选用可配置端口（默认 801/802）。
 */
#include "portal_endpoint.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

void PortalConfigDefaults(PortalConfig *cfg) {
	cfg->eportal_http_port = 801;
	cfg->eportal_https_port = 802;
}

void PortalDrcomLoginDefaults(PortalDrcomLoginConfig *cfg) {
	memset(cfg, 0, sizeof(*cfg));
	cfg->page_index = 0;
	cfg->terminal_type = 1;
	cfg->mac_type = 0;

	/* 官方前端：R6: f0.R6 ? f0.R6.value : 0 —— 无字段时默认 "0" */
	strcpy(cfg->r6, "0");
	strcpy(cfg->js_version, PORTAL_JS_VERSION);
}

static int portal_escape(CURL *handle, const char *in,
		char *out, size_t outlen) {
	char *enc = curl_easy_escape(handle, in, 0);
	size_t n;

	if (!enc)
		return -1;

	n = strlen(enc);
	if (n >= outlen) {
		curl_free(enc);
		return -1;
	}

	memcpy(out, enc, n + 1);
	curl_free(enc);
	return 0;
}

int PortalBuildKernelStatusURL(const PortalLocationInfo *loc,
		char *out, size_t outlen) {
	int n = snprintf(out, outlen,
			"%s/drcom/chkstatus?callback=dr&jsVersion="
			PORTAL_JS_VERSION "&lang=zh",
			loc->origin);

	return (n < 0 || (size_t) n >= outlen) ? -1 : 0;
}

int PortalBuildKernelLogoutURL(const PortalLocationInfo *loc,
		char *out, size_t outlen) {
	int n = snprintf(out, outlen,
			"%s/drcom/logout?callback=dr&jsVersion="
			PORTAL_JS_VERSION "&lang=zh",
			loc->origin);

	return (n < 0 || (size_t) n >= outlen) ? -1 : 0;
}

int PortalBuildDrcomLoginURL(const PortalLocationInfo *loc,
		const PortalDrcomLoginConfig *cfg,
		const char *callback, unsigned int nonce,
		const char *account, const char *password,
		char *out, size_t outlen) {
	CURL *handle;
	char enc_account[PORTAL_ENC_MAX];
	char enc_password[PORTAL_ENC_MAX];
	char enc_r1[PORTAL_ENC_MAX];
	char enc_r2[PORTAL_ENC_MAX];
	char enc_r3[PORTAL_ENC_MAX];
	char enc_r6[PORTAL_ENC_MAX];
	char enc_para[PORTAL_ENC_MAX];
	char enc_v6ip[PORTAL_ENC_MAX];
	char enc_program[PORTAL_ENC_MAX];
	char enc_jsver[64];
	char enc_callback[64];
	char program_pair[256];
	int n;

	if (!cfg || !loc)
		return -1;

	handle = curl_easy_init();
	if (!handle)
		return -1;

	if (portal_escape(handle, account ? account : "",
			enc_account, sizeof(enc_account)) != 0 ||
			portal_escape(handle, password ? password : "",
			enc_password, sizeof(enc_password)) != 0 ||
			portal_escape(handle, cfg->r1, enc_r1, sizeof(enc_r1)) != 0 ||
			portal_escape(handle, cfg->r2, enc_r2, sizeof(enc_r2)) != 0 ||
			portal_escape(handle, cfg->r3, enc_r3, sizeof(enc_r3)) != 0 ||
			portal_escape(handle, cfg->r6, enc_r6, sizeof(enc_r6)) != 0 ||
			portal_escape(handle, cfg->para, enc_para, sizeof(enc_para)) != 0 ||
			portal_escape(handle, cfg->v6ip, enc_v6ip, sizeof(enc_v6ip)) != 0 ||
			portal_escape(handle, cfg->program_index,
			enc_program, sizeof(enc_program)) != 0 ||
			portal_escape(handle,
			cfg->js_version[0] ? cfg->js_version : PORTAL_JS_VERSION,
			enc_jsver, sizeof(enc_jsver)) != 0 ||
			portal_escape(handle, callback && callback[0] ? callback : "dr",
			enc_callback, sizeof(enc_callback)) != 0) {
		curl_easy_cleanup(handle);
		return -1;
	}

	curl_easy_cleanup(handle);

	/* program_index 属于页面运行时数据：override 为空则不携带 */
	if (cfg->program_index[0])
		snprintf(program_pair, sizeof(program_pair),
				"&program_index=%s", enc_program);
	else
		program_pair[0] = 0;

	n = snprintf(out, outlen,
			"%s/drcom/login?callback=%s"
			"&DDDDD=%s&upass=%s&0MKKey=123456"
			"&R1=%s&R2=%s&R3=%s&R6=%s&para=%s&v6ip=%s"
			"&terminal_type=%d&mac_type=%d"
			"%s&page_index=%d&jsVersion=%s&v=%u&lang=zh",
			loc->origin, enc_callback,
			enc_account, enc_password,
			enc_r1, enc_r2, enc_r3, enc_r6, enc_para, enc_v6ip,
			cfg->terminal_type, cfg->mac_type,
			program_pair, cfg->page_index,
			enc_jsver, nonce);

	return (n < 0 || (size_t) n >= outlen) ? -1 : 0;
}

int PortalBuildEportalLoginURL(const PortalLocationInfo *loc,
		const PortalConfig *cfg,
		const char *account, const char *password,
		const char *ip, const char *mac,
		char *out, size_t outlen) {
	CURL *handle;
	char enc_account[PORTAL_ENC_MAX];
	char enc_password[PORTAL_ENC_MAX];
	unsigned int eport;
	int n;

	if (!cfg || !loc)
		return -1;

	eport = loc->tls ? cfg->eportal_https_port : cfg->eportal_http_port;

	handle = curl_easy_init();
	if (!handle)
		return -1;

	if (portal_escape(handle, account ? account : "",
			enc_account, sizeof(enc_account)) != 0 ||
			portal_escape(handle, password ? password : "",
			enc_password, sizeof(enc_password)) != 0) {
		curl_easy_cleanup(handle);
		return -1;
	}

	curl_easy_cleanup(handle);

	/* ePortal 登录端口与 Location 端口无关：
	 * authority 必须用 scheme://host:eport 重建，
	 * 不能在 origin（可能已带显式端口）上再追加 ':eport' */
	n = snprintf(out, outlen,
			"%s://%s:%u/eportal/portal/login?callback=dr&login_method=1"
			"&user_account=%s&user_password=%s"
			"&wlan_user_ip=%s&wlan_user_ipv6=&wlan_user_mac=%s"
			"&wlan_ac_ip=&wlan_ac_name=&terminal_type=1"
			"&jsVersion=" PORTAL_JS_VERSION "&lang=zh",
			loc->scheme, loc->host, eport,
			enc_account, enc_password,
			ip ? ip : "", mac ? mac : "");

	return (n < 0 || (size_t) n >= outlen) ? -1 : 0;
}
