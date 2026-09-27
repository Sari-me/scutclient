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

void PortalConfigDefaults(PortalConfig *cfg);

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

#endif /* __PORTAL_ENDPOINT_H__ */
