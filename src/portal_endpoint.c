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

	n = snprintf(out, outlen,
			"%s:%u/eportal/portal/login?callback=dr&login_method=1"
			"&user_account=%s&user_password=%s"
			"&wlan_user_ip=%s&wlan_user_ipv6=&wlan_user_mac=%s"
			"&wlan_ac_ip=&wlan_ac_name=&terminal_type=1"
			"&jsVersion=" PORTAL_JS_VERSION "&lang=zh",
			loc->origin, eport,
			enc_account, enc_password,
			ip ? ip : "", mac ? mac : "");

	return (n < 0 || (size_t) n >= outlen) ? -1 : 0;
}
