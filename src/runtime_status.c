#include "runtime_status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STATE_PATH_MAX (256)
#define STATE_TMP_MAX (STATE_PATH_MAX + 8)

static char state_path[STATE_PATH_MAX] = {0};
static char cur_state[32] = "";
static char cur_detail[64] = "";
static int state_enabled = 0;

static int instance_name_valid(const char *instance) {
	const char *p;

	if (!instance || !instance[0])
		return 0;

	for (p = instance; *p; p++) {
		if (!((*p >= 'a' && *p <= 'z') ||
		      (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') ||
		      *p == '_' || *p == '-'))
			return 0;
	}

	return 1;
}

int RuntimeStatusInit(const char *instance) {
	state_enabled = 0;
	state_path[0] = 0;
	cur_state[0] = 0;
	cur_detail[0] = 0;

	if (!instance_name_valid(instance))
		return -1;

	mkdir(RUNTIME_STATE_DIR, 0755);

	int n = snprintf(state_path, sizeof(state_path), "%s/%s.state",
			RUNTIME_STATE_DIR, instance);
	if (n < 0 || n >= (int)sizeof(state_path)) {
		state_path[0] = 0;
		return -1;
	}

	state_enabled = 1;
	return 0;
}

static int write_state(const char *state, const char *detail, long heartbeat) {
	char tmp_path[STATE_TMP_MAX];
	FILE *fp;
	int n;

	if (!state_enabled || !state_path[0])
		return -1;

	n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", state_path);
	if (n < 0 || n >= (int)sizeof(tmp_path))
		return -1;

	fp = fopen(tmp_path, "w");
	if (!fp)
		return -1;

	fprintf(fp, "state=%s\n", state);
	fprintf(fp, "detail=%s\n", detail ? detail : "");
	fprintf(fp, "updated=%ld\n", (long)time(NULL));

	if (heartbeat > 0)
		fprintf(fp, "heartbeat=%ld\n", heartbeat);

	fflush(fp);
	fclose(fp);

	/* 原子替换，避免 LuCI 读到半写文件 */
	if (rename(tmp_path, state_path) != 0)
		return -1;

	return 0;
}

int RuntimeStatusSet(const char *state, const char *detail) {
	if (!state || !state[0] ||
			strlen(state) >= sizeof(cur_state) ||
			(detail && strlen(detail) >= sizeof(cur_detail)))
		return -1;

	strcpy(cur_state, state);

	if (detail)
		strcpy(cur_detail, detail);
	else
		cur_detail[0] = 0;

	return write_state(cur_state, cur_detail, 0);
}

int RuntimeStatusHeartbeat(void) {
	return write_state(cur_state, cur_detail, (long)time(NULL));
}

void RuntimeStatusClose(const char *final_state, const char *detail) {
	if (final_state && final_state[0] &&
			strlen(final_state) < sizeof(cur_state) &&
			(!detail || strlen(detail) < sizeof(cur_detail))) {
		strcpy(cur_state, final_state);

		if (detail)
			strcpy(cur_detail, detail);
		else
			cur_detail[0] = 0;

		write_state(cur_state, cur_detail, 0);
	}

	state_enabled = 0;
}
