#ifndef __INFO_H__
#define __INFO_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <getopt.h>

extern struct in_addr udpserver_ipaddr;
extern struct in_addr dns_ipaddr;
extern char *UserName;
extern char *Password;
extern char *OnlineHookCmd;
extern char *OfflineHookCmd;
extern char DeviceName[IFNAMSIZ];
extern char HostName[32];
extern char *Hash;
extern unsigned char Version[64];
extern int Version_len;

/* Per-instance tunables (CLI overridable) */
extern int HBInterval;   /* Dr.com UDP heartbeat interval, seconds */
extern int HBTimeout;    /* Dr.com UDP heartbeat timeout, seconds */
extern int EAPTimeout;   /* 802.1X receive timeout, seconds */
extern int EAPRetries;   /* 802.1X retry times */

/* Authentication method of this instance. */
#define AUTH_DOT1X  0
#define AUTH_PORTAL 1
extern int AuthMethod;            /* AUTH_DOT1X or AUTH_PORTAL */
extern const char *PortalLocation; /* full captive portal Location URL */

/* Web Portal tunables (CLI overridable) */
extern const char *PortalSuffix;  /* appended to the account, e.g. "@dx" */
extern int PortalConnectTimeout;  /* curl connect timeout, seconds */
extern int PortalTimeout;         /* curl request timeout, seconds */
extern int PortalCheckInterval;   /* online check / relogin interval, seconds */
extern int PortalTlsVerify;       /* verify https:// portal certificates */

/* Expected interface MAC (mac spoof verification). */
extern unsigned char ExpectedMAC[6];
extern int HaveExpectedMAC;

int hexStrToByte(const char* source, unsigned char* dest, int bufLen);
#endif

