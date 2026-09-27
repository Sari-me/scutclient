/* File: portal_url.c
 * ------------
 * 基于 libcurl URL API（curl_url，libcurl >= 7.62）的 Location 解析。
 * 支持显式端口、IPv6 authority、path/query 完整保留，
 * 避免手写 parser 遇 ':' 截断 IPv6 / 端口的问题。
 */
#include "portal_url.h"

#include <curl/curl.h>
#include <curl/urlapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int PortalLocationQueryGet(const PortalLocationInfo *info, const char *key,
		char *out, size_t outlen) {
	const char *p;
	size_t keylen;
	CURL *curl;
	char *decoded;
	int decoded_len;
	int ret = -1;

	if (!info || !key || !key[0] || !out || !outlen)
		return -1;
	out[0] = 0;
	keylen = strlen(key);
	p = info->query;
	while (*p) {
		const char *end = strchr(p, '&');
		const char *value;
		size_t len;

		if (!end)
			end = p + strlen(p);
		if ((size_t) (end - p) > keylen &&
				memcmp(p, key, keylen) == 0 && p[keylen] == '=') {
			char encoded[PORTAL_QUERY_MAX];
			size_t i;
			value = p + keylen + 1;
			len = (size_t) (end - value);
			if (len >= sizeof(encoded))
				return -1;
			memcpy(encoded, value, len);
			for (i = 0; i < len; i++)
				if (encoded[i] == '+')
					encoded[i] = ' ';
			encoded[len] = 0;
			curl = curl_easy_init();
			if (!curl)
				return -1;
			decoded = curl_easy_unescape(curl, encoded, (int) len,
					&decoded_len);
			if (decoded && decoded_len > 0 &&
					(size_t) decoded_len < outlen &&
					!memchr(decoded, 0, (size_t) decoded_len)) {
				memcpy(out, decoded, (size_t) decoded_len);
				out[decoded_len] = 0;
				ret = 0;
			}
			if (decoded)
				curl_free(decoded);
			curl_easy_cleanup(curl);
			return ret;
		}
		p = *end ? end + 1 : end;
	}
	return -1;
}

int PortalLocationParse(const char *location, PortalLocationInfo *info) {
	CURLU *url;
	char *scheme = NULL;
	char *host = NULL;
	char *port = NULL;
	char *explicit_port = NULL;
	char *path = NULL;
	char *query = NULL;
	char *user = NULL;
	int ret = -1;

	if (!location || !location[0] || !info)
		return -1;

	memset(info, 0, sizeof(*info));

	if (strlen(location) >= PORTAL_URL_MAX)
		return -1;

	strcpy(info->raw, location);

	url = curl_url();
	if (!url)
		return -1;

	if (curl_url_set(url, CURLUPART_URL, location, 0) != CURLUE_OK)
		goto out;

	/* Location 来自门户重定向，不应携带 userinfo 凭据 */
	if (curl_url_get(url, CURLUPART_USER, &user, 0) == CURLUE_OK) {
		int has_user = user && user[0];
		curl_free(user);
		user = NULL;
		if (has_user)
			goto out;
	}

	if (curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK)
		goto out;

	if (!strcmp(scheme, "https")) {
		info->tls = 1;
		strcpy(info->scheme, "https");
	} else if (!strcmp(scheme, "http")) {
		info->tls = 0;
		strcpy(info->scheme, "http");
	} else {
		/* file/ftp/data/javascript 等一律拒绝 */
		goto out;
	}

	if (curl_url_get(url, CURLUPART_HOST, &host, 0) != CURLUE_OK)
		goto out;

	if (!host[0] || strlen(host) >= PORTAL_HOST_MAX)
		goto out;
	strcpy(info->host, host);

	/* 显式端口：未写端口时该调用返回 CURLUE_NO_PORT */
	if (curl_url_get(url, CURLUPART_PORT, &explicit_port, 0) == CURLUE_OK &&
			explicit_port && explicit_port[0]) {
		info->has_explicit_port = 1;
	}

	/* 有效端口：scheme 默认端口兜底 */
	if (curl_url_get(url, CURLUPART_PORT, &port,
			CURLU_DEFAULT_PORT) == CURLUE_OK && port && port[0]) {
		info->port = (unsigned int) atoi(port);
	}

	if (info->port == 0)
		goto out;

	if (curl_url_get(url, CURLUPART_PATH, &path, 0) == CURLUE_OK &&
			path && strlen(path) < PORTAL_PATH_MAX) {
		strcpy(info->path, path);
	} else {
		strcpy(info->path, "/");
	}

	if (curl_url_get(url, CURLUPART_QUERY, &query, 0) == CURLUE_OK &&
			query) {
		strncpy(info->query, query, PORTAL_QUERY_MAX - 1);
	}

	/* origin：scheme://host[:显式端口]（默认端口按方案示例省略） */
	if (info->has_explicit_port)
		snprintf(info->origin, PORTAL_ORIGIN_MAX, "%s://%s:%u",
				info->scheme, info->host, info->port);
	else
		snprintf(info->origin, PORTAL_ORIGIN_MAX, "%s://%s",
				info->scheme, info->host);

	ret = 0;

out:
	if (scheme)
		curl_free(scheme);
	if (host)
		curl_free(host);
	if (port)
		curl_free(port);
	if (explicit_port)
		curl_free(explicit_port);
	if (path)
		curl_free(path);
	if (query)
		curl_free(query);
	if (user)
		curl_free(user);

	curl_url_cleanup(url);
	return ret;
}
