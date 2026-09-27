/* File: portal_endpoint.h
 * ------------
 * 注：Portal API endpoint 统一构造层。
 * 所有 chkstatus / logout / login URL 只能在这里生成——
 * scheme/host/端口全部来自解析后的 Location 与协议可覆盖端口，
 * 源码不包含任何真实学校 Portal 地址。
 */
#ifndef __PORTAL_ENDPOINT_H__
#define __PORTAL_ENDPOINT_H__

#include <stddef.h>

#include "portal_url.h"

#define PORTAL_JS_VERSION "4.1.3"
#define PORTAL_ENC_MAX    1536

typedef struct {
	unsigned int eportal_http_port;  /* Location 为 http 时登录端口 */
	unsigned int eportal_https_port; /* Location 为 https 时登录端口 */
} PortalConfig;

/* Dr.COM Web（/drcom/login）登录参数。program_index 属于页面运行时
 * 数据：允许 UCI/CLI override，默认留空（协议不要求写死某个值）。 */
typedef struct {
	char program_index[128];
	int page_index;
	char js_version[32];
	char r1[64];
	char r2[64];
	char r3[128];
	char r6[64];
	char para[256];
	char v6ip[128];
	int terminal_type;
	int mac_type;
	int enable_md5;   /* 预留：bootstrap 发现 en_md5 后启用 */
	int enable_r3;    /* 预留：login_method=0 且 enable_r3 时运营商走 R3 */
} PortalDrcomLoginConfig;

void PortalConfigDefaults(PortalConfig *cfg);
void PortalDrcomLoginDefaults(PortalDrcomLoginConfig *cfg);

int PortalBuildKernelStatusURL(const PortalLocationInfo *loc,
		char *out, size_t outlen);

int PortalBuildKernelLogoutURL(const PortalLocationInfo *loc,
		char *out, size_t outlen);

/* account/password 为原文，内部用 curl_easy_escape 编码 */
int PortalBuildEportalLoginURL(const PortalLocationInfo *loc,
		const PortalConfig *cfg,
		const char *account, const char *password,
		const char *ip, const char *mac,
		char *out, size_t outlen);

/* Dr.COM Web 登录（<origin>/drcom/login）。
 * callback/nonce 运行时生成；program_index 仅在 override 非空时携带。 */
int PortalBuildDrcomLoginURL(const PortalLocationInfo *loc,
		const PortalDrcomLoginConfig *cfg,
		const char *callback, unsigned int nonce,
		const char *account, const char *password,
		char *out, size_t outlen);

#endif /* __PORTAL_ENDPOINT_H__ */
