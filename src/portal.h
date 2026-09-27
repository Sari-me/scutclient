/* File: portal.h
 * ------------
 * 注：Web Portal（Dr.COM 哆点门户 4.x eportal/Radius 方式）认证后端。
 */
#ifndef __PORTAL_H__
#define __PORTAL_H__

/* portal 认证主循环：在线检测 + 自动重登录，不返回。 */
void PortalRun(void);

/* 发送一次 portal 注销请求（SIGTERM / --logoff 退出路径调用）。 */
void PortalLogout(void);

#endif /* __PORTAL_H__ */
