#include "tracelog.h"
#include <errno.h>

/* 默认日志最大 256KB，保留 2 份轮转 */
#define DEFAULT_LOG_SIZE (256 * 1024)
#define DEFAULT_LOG_KEEP 2

static char logtime[20];
char instance_name[MAXFILENAME] = "-";
char filepath[MAXFILEPATH] = "/tmp/scutclient.log";
static FILE *logfile = NULL;
static int file_log_enabled = 0;
static unsigned long log_max_size = DEFAULT_LOG_SIZE;
static int log_keep = DEFAULT_LOG_KEEP;
LOGLEVEL cloglev = INF;

const static char *LogLevelText[] =
		{ "NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE" };
const static char *LogTypeText[] =
		{ "ALL", "INIT", "8021X", "DRCOM" };

static unsigned long get_file_size(const char *path) {
	struct stat statbuff;

	if (stat(path, &statbuff) < 0)
		return 0;
	return (unsigned long) statbuff.st_size;
}

/*
 *获取时间
 * */
static void settime() {
	time_t timer = time(NULL);
	strftime(logtime, 20, "%Y-%m-%d %H:%M:%S", localtime(&timer));
}

static void log_prefix(FILE *fp, LOGTYPE logtype, LOGLEVEL loglevel) {
	fprintf(fp, "%s [%s] [%d] [%-5s] [%-5s] ",
			logtime, instance_name, (int) getpid(),
			LogTypeText[logtype], LogLevelText[loglevel]);
}

static void LogRotate(void) {
	char from[MAXFILEPATH + 16];
	char to[MAXFILEPATH + 16];
	int i;

	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}

	snprintf(to, sizeof(to), "%s.%d", filepath, log_keep);
	unlink(to);
	for (i = log_keep - 1; i >= 1; i--) {
		snprintf(from, sizeof(from), "%s.%d", filepath, i);
		snprintf(to, sizeof(to), "%s.%d", filepath, i + 1);
		rename(from, to);
	}
	snprintf(to, sizeof(to), "%s.1", filepath);
	rename(filepath, to);

	logfile = fopen(filepath, "a+");
}

int LogInit(const char *inst, const char *file, LOGLEVEL level,
		unsigned long maxsize, int keep) {
	char dirbuf[MAXFILEPATH];
	char *slash;

	if (inst && inst[0]) {
		strncpy(instance_name, inst, sizeof(instance_name) - 1);
		instance_name[sizeof(instance_name) - 1] = 0;
	}

	if (level > NONE && level <= TRACE)
		cloglev = level;

	if (maxsize > 0)
		log_max_size = maxsize;

	if (keep > 0)
		log_keep = keep;

	if (file && file[0] && strcmp(file, "off") == 0) {
		/* 显式关闭文件日志：仅输出 stdout（procd/system log） */
		file_log_enabled = 0;
	} else {
		if (file && file[0]) {
			strncpy(filepath, file, sizeof(filepath) - 1);
			filepath[sizeof(filepath) - 1] = 0;
		} else {
			strcpy(filepath, "/tmp/scutclient.log");
		}

		/* 确保日志目录存在（如 /tmp/scutclient/） */
		strncpy(dirbuf, filepath, sizeof(dirbuf) - 1);
		dirbuf[sizeof(dirbuf) - 1] = 0;
		slash = strrchr(dirbuf, '/');
		if (slash && slash != dirbuf) {
			*slash = 0;
			mkdir(dirbuf, 0755);
		}

		logfile = fopen(filepath, "a+");
		if (logfile == NULL) {
			perror("Unable to open log file");
			return -1;
		}
		file_log_enabled = 1;
	}

	return 0;
}

void LogClose(void) {
	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}
	file_log_enabled = 0;
}

/*
 *日志写入：单条同时输出到 stdout（procd/system log）与文件。
 *格式：时间 [实例] [PID] [模块] [等级] 消息
 * */
int LogWrite(LOGTYPE logtype, LOGLEVEL loglevel, char *format, ...) {
	va_list args;

	if (loglevel > cloglev)
		return 0;

	settime();

	if (file_log_enabled && get_file_size(filepath) > log_max_size)
		LogRotate();

	log_prefix(stdout, logtype, loglevel);
	if (logfile)
		log_prefix(logfile, logtype, loglevel);

	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");

	if (logfile) {
		va_start(args, format);
		vfprintf(logfile, format, args);
		va_end(args);
		fprintf(logfile, "\n");
		fflush(logfile);
	}

	return 0;
}
