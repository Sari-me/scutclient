/* File: main.c
 * ------------
 * 校园网802.1X客户端命令行
 */
#include "auth.h"
#include "info.h"
#include "tracelog.h"
#include "runtime_status.h"
#include "portal.h"
#include <signal.h>

struct sigaction sa_term;

// 命令行参数列表
static const struct option long_options[] = {
	{"username", required_argument, NULL, 'u'},
	{"password", required_argument, NULL, 'p'},
	{"iface", required_argument, NULL, 'i'},
	{"dns", required_argument, NULL, 'n'},
	{"hostname", required_argument, NULL, 'H'},
	{"udp-server", required_argument, NULL, 's'},
	{"cli-version", required_argument,NULL, 'c'},
	{"net-time", required_argument,NULL, 'T'},
	{"hash", required_argument, NULL, 'h'},
	{"online-hook", required_argument, NULL, 'E'},
	{"offline-hook", required_argument, NULL, 'Q'},
	{"heartbeat-interval", required_argument, NULL, 1000},
	{"heartbeat-timeout", required_argument, NULL, 1001},
	{"eap-timeout", required_argument, NULL, 1002},
	{"eap-retries", required_argument, NULL, 1003},
	{"expected-mac", required_argument, NULL, 1004},
	{"instance", required_argument, NULL, 1005},
	{"log-level", required_argument, NULL, 1006},
	{"log-file", required_argument, NULL, 1007},
	{"log-max-size", required_argument, NULL, 1008},
	{"log-keep", required_argument, NULL, 1009},
	{"auth-method", required_argument, NULL, 1010},
	{"portal-location", required_argument, NULL, 1011},
	{"portal-suffix", required_argument, NULL, 1012},
	{"portal-connect-timeout", required_argument, NULL, 1013},
	{"portal-timeout", required_argument, NULL, 1014},
	{"portal-check-interval", required_argument, NULL, 1015},
	{"portal-tls-verify", required_argument, NULL, 1016},
	{"portal-http-port", required_argument, NULL, 1017},
	{"portal-https-port", required_argument, NULL, 1018},
	{"portal-login-mode", required_argument, NULL, 1019},
	{"portal-program-index", required_argument, NULL, 1020},
	{"portal-page-index", required_argument, NULL, 1021},
	{"portal-js-version", required_argument, NULL, 1022},
	{"portal-r3", required_argument, NULL, 1023},
	{"route-isolation", required_argument, NULL, 1024},
	{"debug", optional_argument, NULL, 'D'},
	{"logoff", no_argument, NULL, 'o'},
	{NULL, no_argument, NULL, 0}
};

void PrintHelp(const char * argn) {
	printf("Usage: %s --username <username> --password <password> [options...]\n"
		" -i, --iface <ifname> Interface to perform authentication.\n"
		" -n, --dns <dns> DNS server address to be sent to UDP server.\n"
		" -H, --hostname <hostname>\n"
		" -s, --udp-server <server>\n"
		" -c, --cli-version <client version>\n"
		" -T, --net-time <time> The time you are allowed to access internet. e.g. 6:10\n"
		" -h, --hash <hash> DrAuthSvr.dll hash value.\n"
		" -E, --online-hook <command> Command to be execute after EAP authentication success.\n"
		" -Q, --offline-hook <command> Command to be execute when you are forced offline at nignt.\n"
		"     --heartbeat-interval <sec> Dr.com UDP heartbeat interval. Default 12.\n"
		"     --heartbeat-timeout <sec> Dr.com UDP heartbeat timeout. Default 2.\n"
		"     --eap-timeout <sec> 802.1X receive timeout. Default 1.\n"
		"     --eap-retries <times> 802.1X retry times. Default 3.\n"
		"     --expected-mac <mac> Abort if the interface MAC does not match.\n"
		"     --instance <id> Instance name written into every log line.\n"
		"     --log-level <error|warn|info|debug|trace> Log level. Default info.\n"
		"     --log-file <path|off> Per-instance log file. Default /tmp/scutclient.log.\n"
		"     --log-max-size <bytes> Rotate the log beyond this size. Default 262144.\n"
		"     --log-keep <n> Rotated log files to keep. Default 2.\n"
		"     --auth-method <dot1x|portal> Authentication method. Default dot1x.\n"
		"     --portal-location <url> Captive portal Location URL (portal mode).\n"
		"     --portal-suffix <suffix> Appended to the account, e.g. @dx (portal).\n"
		"     --portal-connect-timeout <sec> Portal connect timeout. Default 5.\n"
		"     --portal-timeout <sec> Portal request timeout. Default 10.\n"
		"     --portal-check-interval <sec> Portal online check interval. Default 15.\n"
		"     --portal-tls-verify <0|1> Verify https portal certificates. Default 1.\n"
		"     --portal-http-port <port> ePortal login port for http Locations. Default 801.\n"
		"     --portal-https-port <port> ePortal login port for https Locations. Default 802.\n"
		"     --portal-login-mode <auto|drcom|eportal> Portal login backend. Default auto.\n"
		"     --portal-program-index <idx> Dr.COM Web program index override.\n"
		"     --portal-page-index <n> Dr.COM Web page index override.\n"
		"     --portal-js-version <ver> Dr.COM Web jsVersion override. Default 4.1.3.\n"
		"     --portal-r3 <value> Dr.COM Web R3 override.\n"
		"     --route-isolation <native|mwan3> How the instance is launched.\n"
		" -D, --debug [level] Enable debug output (numeric level 0-5).\n"
		" -o, --logoff\n",
		argn);
}

void handle_term(int signal) {
	LogWrite(ALL, INF, "Exiting...");

	if (AuthMethod == AUTH_PORTAL)
		PortalLogout();
	else
		auth_8021x_Logoff();

	RuntimeStatusClose("stopped", "terminated");
	LogClose();
	exit(0);
}

int main(int argc, char *argv[]) {
	int client = 1;
	int ch, tmpdbg;
	uint8_t a_hour = 255, a_minute = 255;
	int ret;
	unsigned int retry_time = 1;
	time_t ctime;
	struct tm * cltime;

	const char *inst_name = NULL;
	const char *loglev_str = NULL;
	const char *logfile_str = NULL;
	unsigned long logmax = 0;
	int logkeep = 0;
	int dbgflag = 0;
	int dbglevel = -1;
	LOGLEVEL init_level = INF;

	while ((ch = getopt_long(argc, argv, "u:p:i:n:H:s:c:T:h:E:Q:D::o",
			long_options, NULL)) != -1) {
		switch (ch) {
		case 'u':
			UserName = optarg;
			break;
		case 'p':
			Password = optarg;
			break;
		case 'E':
			OnlineHookCmd = optarg;
			break;
		case 'Q':
			OfflineHookCmd = optarg;
			break;
		case 'i':
			strncpy(DeviceName, optarg, IFNAMSIZ);
			break;
		case 'n':
			if (!inet_aton(optarg, &dns_ipaddr)) {
				LogWrite(INIT, ERROR, "DNS invalid!");
				exit(-1);
			}
			break;
		case 'H':
			strncpy(HostName, optarg, 32);
			break;
		case 's':
			if (!inet_aton(optarg, &udpserver_ipaddr)) {
				LogWrite(INIT, ERROR, "UDP server IP invalid!");
				exit(-1);
			}
			break;
		case 'T':
			if((sscanf(optarg, "%hhu:%hhu", &a_hour, &a_minute) != 2) || (a_hour >= 24) || (a_minute >= 60)) {
				LogWrite(INIT, ERROR, "Time invalid!");
				exit(-1);
			}
			break;
		case 'c':
			Version_len = hexStrToByte(optarg, Version, sizeof(Version));
			break;
		case 'h':
			Hash = optarg;
			break;
		case 1000:
			HBInterval = atoi(optarg);
			break;
		case 1001:
			HBTimeout = atoi(optarg);
			break;
		case 1002:
			EAPTimeout = atoi(optarg);
			break;
		case 1003:
			EAPRetries = atoi(optarg);
			break;
		case 1004:
			if (sscanf(optarg, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
					&ExpectedMAC[0], &ExpectedMAC[1],
					&ExpectedMAC[2], &ExpectedMAC[3],
					&ExpectedMAC[4], &ExpectedMAC[5]) != 6) {
				LogWrite(INIT, ERROR, "Expected MAC invalid!");
				exit(-1);
			}
			HaveExpectedMAC = 1;
			break;
		case 1005:
			inst_name = optarg;
			break;
		case 1006:
			loglev_str = optarg;
			break;
		case 1007:
			logfile_str = optarg;
			break;
		case 1008:
			logmax = (unsigned long) atol(optarg);
			break;
		case 1009:
			logkeep = atoi(optarg);
			break;
		case 1010:
			if (!strcmp(optarg, "dot1x")) {
				AuthMethod = AUTH_DOT1X;
			} else if (!strcmp(optarg, "portal")) {
				AuthMethod = AUTH_PORTAL;
			} else {
				LogWrite(INIT, ERROR, "Invalid auth method '%s'!", optarg);
				exit(-1);
			}
			break;
		case 1011:
			PortalLocation = optarg;
			break;
		case 1012:
			PortalSuffix = optarg;
			break;
		case 1013:
			PortalConnectTimeout = atoi(optarg);
			break;
		case 1014:
			PortalTimeout = atoi(optarg);
			break;
		case 1015:
			PortalCheckInterval = atoi(optarg);
			break;
		case 1016:
			PortalTlsVerify = atoi(optarg);
			break;
		case 1017:
			PortalEportalHttpPort = atoi(optarg);
			break;
		case 1018:
			PortalEportalHttpsPort = atoi(optarg);
			break;
		case 1019:
			if (!strcmp(optarg, "auto")) {
				PortalLoginBackend = PORTAL_LOGIN_AUTO;
			} else if (!strcmp(optarg, "drcom")) {
				PortalLoginBackend = PORTAL_LOGIN_DRCOM;
			} else if (!strcmp(optarg, "eportal")) {
				PortalLoginBackend = PORTAL_LOGIN_EPORTAL;
			} else {
				LogWrite(INIT, ERROR, "Invalid portal login mode '%s'!", optarg);
				exit(-1);
			}
			break;
		case 1020:
			PortalProgramIndex = optarg;
			break;
		case 1021:
			PortalPageIndex = atoi(optarg);
			break;
		case 1022:
			PortalJsVersion = optarg;
			break;
		case 1023:
			PortalR3 = optarg;
			break;
		case 1024:
			RouteIsolation = optarg;
			break;
		case 'D':
			if (optarg) {
				tmpdbg = atoi(optarg);
				if ((tmpdbg < NONE) || (tmpdbg > TRACE)) {
					LogWrite(INIT, ERROR, "Invalid debug level!");
				} else {
					dbglevel = tmpdbg;
				}
			} else {
				dbgflag = 1;
			}
			break;
		case 'o':
			client = LOGOFF;
			break;
		default:
			PrintHelp(argv[0]);
			exit(-1);
			break;
		}
	}

	if (HostName[0] == 0)
		gethostname(HostName, sizeof(HostName));

	if (udpserver_ipaddr.s_addr == 0)
		inet_aton(SERVER_ADDR, &udpserver_ipaddr);
	if (dns_ipaddr.s_addr == 0)
		inet_aton(DNS_ADDR, &dns_ipaddr);

	/* 日志初始化（等级/文件/轮转），必须先于一切校验与横幅输出，
	 * 保证启动异常也能如实写入实例日志与状态文件 */
	if (loglev_str) {
		if (!strcmp(loglev_str, "error")) {
			init_level = ERROR;
		} else if (!strcmp(loglev_str, "warn")) {
			init_level = WARN;
		} else if (!strcmp(loglev_str, "info")) {
			init_level = INF;
		} else if (!strcmp(loglev_str, "debug")) {
			init_level = DEBUG;
		} else if (!strcmp(loglev_str, "trace")) {
			init_level = TRACE;
		} else {
			LogWrite(INIT, ERROR, "Invalid log level '%s'!", loglev_str);
			exit(-1);
		}
	} else if (dbglevel >= 0) {
		init_level = (LOGLEVEL) dbglevel;
	} else if (dbgflag) {
		init_level = DEBUG;
	}

	LogInit(inst_name, logfile_str, init_level, logmax, logkeep);

	RuntimeStatusInit(inst_name);
	RuntimeStatusSet("starting", "process_started");
	RuntimeStatusSetField("auth_method",
			AuthMethod == AUTH_PORTAL ? "portal" : "dot1x");
	RuntimeStatusSetField("device", DeviceName);
	RuntimeStatusSetField("route_isolation",
			RouteIsolation ? RouteIsolation : "native");

	/* 以下校验在日志/状态就绪后执行：失败原因可完整落盘 */
	if ((client != LOGOFF) &&
			!((UserName && Password && UserName[0] && Password[0]))) {
		RuntimeStatusSet("error", "credentials_missing");
		LogWrite(INIT, ERROR, "Please specify username and password!");
		LogClose();
		exit(-1);
	}

	if (HBInterval < 1 || HBInterval > 3600 ||
			HBTimeout < 1 || HBTimeout > 3600 ||
			EAPTimeout < 1 || EAPTimeout > 3600 ||
			EAPRetries < 1 || EAPRetries > 100) {
		RuntimeStatusSet("error", "parameters_out_of_range");
		LogWrite(INIT, ERROR, "Heartbeat/EAP parameters out of range!");
		LogClose();
		exit(-1);
	}

	/* Portal 后端：在线检测 + 自动重登录主循环（--logoff 时只注销一次） */
	if (AuthMethod == AUTH_PORTAL) {
		if (!PortalLocation ||
				(strncmp(PortalLocation, "http://", 7) != 0 &&
				 strncmp(PortalLocation, "https://", 8) != 0)) {
			RuntimeStatusSet("error", "portal_location_invalid");
			LogWrite(INIT, ERROR,
					"Portal authentication requires a http(s):// Location!");
			LogClose();
			exit(-1);
		}

		if (PortalConnectTimeout < 1 || PortalConnectTimeout > 120 ||
				PortalTimeout < 1 || PortalTimeout > 600 ||
				PortalCheckInterval < 5 || PortalCheckInterval > 3600) {
			RuntimeStatusSet("error", "portal_options_invalid");
			LogWrite(INIT, ERROR, "Portal parameters out of range!");
			LogClose();
			exit(-1);
		}

		if (client == LOGOFF) {
			PortalLogout();
			LogClose();
			return 0;
		}

		PortalRun();
	}

	LogWrite(ALL, INF, "scutclient built at: " __DATE__ " " __TIME__);
	LogWrite(ALL, INF, "Authored by Scutclient Project");
	LogWrite(ALL, INF, "Source code available at https://github.com/Sari-me/scutclient");
	LogWrite(ALL, INF, "Contact us with QQ group 262939451");
	LogWrite(ALL, INF, "#######################################");

	/* 配置退出登录的signal handler */
	sa_term.sa_handler = &handle_term;
	sa_term.sa_flags = SA_RESETHAND;
	sigfillset(&sa_term.sa_mask);
	sigaction(SIGTERM, &sa_term, NULL);
	sigaction(SIGINT, &sa_term, NULL);

	/* 调用子函数完成802.1X认证 */
	while(1) {
		ret = Authentication(client);
		if(ret == 1) {
			retry_time = 1;
			LogWrite(ALL, INF, "Restart authentication.");
		} else if(ret == -ENETUNREACH) {
			LogWrite(ALL, INF, "Retry in %d secs.", retry_time);
			sleep(retry_time);
			if (retry_time <= 256)
				retry_time *= 2;
		} else if(timeNotAllowed && (a_minute < 60)) {
			timeNotAllowed = 0;
			ctime = time(NULL);
			cltime = localtime(&ctime);
			if(((int)a_hour * 60 + a_minute) > ((int)(cltime -> tm_hour) * 60 + cltime -> tm_min)) {
				LogWrite(ALL, INF, "Waiting till %02hhd:%02hhd. Have a good sleep...", a_hour, a_minute);
				if (OfflineHookCmd) {
					system(OfflineHookCmd);
				}
				sleep((((int)a_hour * 60 + a_minute) - ((int)(cltime -> tm_hour) * 60 + cltime -> tm_min)) * 60 - cltime -> tm_sec);
			} else {
				break;
			}
		} else {
			break;
		}
	}
	LogWrite(ALL, ERROR, "Exit.");
	RuntimeStatusClose("stopped", "exit");
	LogClose();
	return 0;
}
