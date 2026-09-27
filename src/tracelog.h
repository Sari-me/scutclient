/* File: tracelog.h
 * ------------
 * 注：日志模块头文件
 */
#ifndef LOGC_H_
#define LOGC_H_
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAXLEN (2048)
#define MAXFILEPATH (512)
#define MAXFILENAME (50)

typedef enum {
	NONE = 0, ERROR = 1, WARN = 2, INF = 3, DEBUG = 4, TRACE = 5
} LOGLEVEL;

typedef enum {
	ALL = 0, INIT = 1, DOT1X = 2, DRCOM = 3
} LOGTYPE;

extern LOGLEVEL cloglev;
extern char instance_name[MAXFILENAME];
extern char filepath[MAXFILEPATH];

/*
 * 初始化日志：实例名、文件路径（NULL/off 表示关闭文件日志）、
 * 等级、轮转大小（字节）与保留份数。必须在任何 LogWrite 之前调用。
 */
int LogInit(const char *inst, const char *file, LOGLEVEL level,
		unsigned long maxsize, int keep);

/* 关闭日志文件句柄（退出/信号路径调用）。 */
void LogClose(void);

int LogWrite(LOGTYPE logtype, LOGLEVEL loglevel, char *format, ...);
#endif /* LOGC_H_ */
