/* File: runtime_status.h
 * ------------
 * 注：实例运行状态文件模块。
 * C 核心把每个实例的认证状态原子写入 /var/run/scutclient/<instance>.state，
 * 由 LuCI 状态页读取。日志会轮转、可关闭且含旧记录，因此状态不允许靠解析日志获得。
 */
#ifndef __RUNTIME_STATUS_H__
#define __RUNTIME_STATUS_H__

#define RUNTIME_STATE_DIR "/var/run/scutclient"

/*
 * 初始化（必须在写状态前调用）。instance 非法或为空时模块保持禁用，
 * 后续 Set/Heartbeat/Close 均为无害空操作。返回 0 表示启用。
 */
int RuntimeStatusInit(const char *instance);

/* 覆盖当前状态（如 starting/authenticating/drcom_starting/online/
 * reconnecting/auth_failed/error/stopped）。 */
int RuntimeStatusSet(const char *state, const char *detail);

/* 记录最近一次成功心跳时间，保留当前状态。 */
int RuntimeStatusHeartbeat(void);

/* 写入终态后禁用模块（进程退出/被终止路径调用）。 */
void RuntimeStatusClose(const char *final_state, const char *detail);

/*
 * 记录附加诊断字段（auth_method/device/source_ipv4/route_isolation/
 * portal_backend 等），随每次状态写入一并落盘。
 * 最多保留 8 组；value 截断到 63 字节。
 */
void RuntimeStatusSetField(const char *key, const char *value);

#endif /* __RUNTIME_STATUS_H__ */
