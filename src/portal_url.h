/* File: portal_url.h
 * ------------
 * 注：Portal Location 的结构化 URL 模型。
 * Location 是运行时事实来源：scheme/host/端口/路径/查询全部动态解析，
 * 源码与默认配置不包含任何真实学校 Portal 地址。
 */
#ifndef __PORTAL_URL_H__
#define __PORTAL_URL_H__

#include <stddef.h>

#define PORTAL_URL_MAX     4096
#define PORTAL_HOST_MAX    256
#define PORTAL_PATH_MAX    2048
#define PORTAL_QUERY_MAX   2048
#define PORTAL_ORIGIN_MAX  1024

typedef struct {
	char raw[PORTAL_URL_MAX];      /* 完整 Location */
	char scheme[16];               /* http / https */
	char host[PORTAL_HOST_MAX];    /* 域名 / IPv4 / [IPv6] */
	unsigned int port;             /* 有效端口（无显式端口时取 scheme 默认） */
	int has_explicit_port;         /* Location 是否显式写了端口 */
	char path[PORTAL_PATH_MAX];    /* 路径，完整保留 */
	char query[PORTAL_QUERY_MAX];  /* 查询串，完整保留（wlanacname 等） */
	char origin[PORTAL_ORIGIN_MAX];/* scheme://host[:显式端口] */
	int tls;                       /* 1 = https */
} PortalLocationInfo;

/*
 * 解析 Location。仅接受 http/https；拒绝其他 scheme、空 host、
 * 带 userinfo（凭据）的 URL。成功返回 0。
 */
int PortalLocationParse(const char *location, PortalLocationInfo *info);

/* 从 Location 查询串读取并 URL 解码一个参数；不存在或无效时返回 -1。 */
int PortalLocationQueryGet(const PortalLocationInfo *info, const char *key,
		char *out, size_t outlen);

#endif /* __PORTAL_URL_H__ */
