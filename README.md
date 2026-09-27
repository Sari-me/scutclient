scutclient
=================

SCUT Dr.com(X) client written in C.

# Compiling
## Using automake (for all distribution)
```bash
git clone https://github.com/scutclient/scutclient.git
cd scutclient
mkdir build && cd build
cmake ..
make
```

## Using OpenWrt buildroot
### Download source code
To compile the latest stable version, you only need **openwrt/Makefile** in the source code. Buildroot will automatically download the source.

If you want to compile the latest git HEAD, you need to clone the entire repository and checkout to the branch/version you need.

### Preparation
#### Using SDK
Download and extract the SDK you need. For example(Snapshots on ar71xx):
```bash
wget https://downloads.openwrt.org/snapshots/targets/ar71xx/generic/openwrt-sdk-ar71xx-generic_gcc-5.5.0_musl.Linux-x86_64.tar.xz
tar -Jxvf openwrt-sdk-ar71xx-generic_gcc-5.5.0_musl.Linux-x86_64.tar.xz
cd openwrt-sdk-ar71xx-generic_gcc-5.5.0_musl.Linux-x86_64
```

### Compiling
#### Using SDK

Execute the following command:

```bash
make defconfig
make package/scutclient/compile V=s
```
The compiled ipk will be placed under **bin** directory.

#### Using OpenWrt source code

Select **scutclient** under **Network** tab and start building your firmware.

# Usage

Each authentication instance runs as one independent process:

```text
one instance = one account + one WAN + one Dr.COM session + one log file
```

On OpenWrt every `config scutclient '<id>'` section in `/etc/config/scutclient`
is spawned as its own procd instance by `/etc/init.d/scutclient`, with its own
log file under `/tmp/scutclient/<id>.log`. Single-instance commands:

```sh
/etc/init.d/scutclient start   <id>
/etc/init.d/scutclient stop    <id>
/etc/init.d/scutclient restart <id>
/etc/init.d/scutclient logoff_instance <id>
```

One WAN (logical interface, or the resolved Linux netdev) can only be bound by
one instance; the init script refuses conflicting instances at start-up. An
instance without an explicit `interface` is skipped, never silently bound to
`wan`.

Startup is two-phase: all instance MACs are prepared and written to
network/wireless UCI first, the network is reloaded **at most once**, then all
WANs are awaited, netdevs are re-resolved and every expected MAC is verified
before the procd instances are spawned. During the network apply a lock file
(`/var/run/scutclient-network-apply.lock`) keeps the hotplug handler quiet.

### Command line
```bash
scutclient --username <username> --password <password> [options...]
 -i, --iface <ifname> Interface to perform authentication.
 -n, --dns <dns> DNS server address to be sent to UDP server.
 -H, --hostname <hostname>
 -s, --udp-server <server>
 -c, --cli-version <client version>
 -T, --net-time <time> The time you are allowed to access internet. e.g. 6:10
 -h, --hash <hash> DrAuthSvr.dll hash value.
 -E, --online-hook <command> Command to be execute after EAP authentication success.
 -Q, --offline-hook <command> Command to be execute when you are forced offline at nignt.
     --heartbeat-interval <sec> Dr.com UDP heartbeat interval. Default 12.
     --heartbeat-timeout <sec> Dr.com UDP heartbeat timeout. Default 2.
     --eap-timeout <sec> 802.1X receive timeout. Default 1.
     --eap-retries <times> 802.1X retry times. Default 3.
     --expected-mac <mac> Abort if the interface MAC does not match.
     --instance <id> Instance name written into every log line.
     --log-level <error|warn|info|debug|trace> Log level. Default info.
     --log-file <path|off> Per-instance log file. Default /tmp/scutclient.log.
     --log-max-size <bytes> Rotate the log beyond this size. Default 262144.
     --log-keep <n> Rotated log files to keep. Default 2.
     --auth-method <dot1x|portal> Authentication method. Default dot1x.
     --mwan3-mark <mark> Bypass mwan3 policy rules for Dr.COM UDP.
     --portal-location <url> Captive portal Location URL (portal mode).
     --portal-suffix <suffix> Appended to the account, e.g. @dx (portal).
     --portal-connect-timeout <sec> Portal connect timeout. Default 5.
     --portal-timeout <sec> Portal request timeout. Default 10.
     --portal-check-interval <sec> Portal online check interval. Default 15.
     --portal-tls-verify <0|1> Verify https portal certificates. Default 1.
     --portal-http-port <port> ePortal login port for http Locations. Default 801.
     --portal-https-port <port> ePortal login port for https Locations. Default 802.
     --portal-login-mode <auto|drcom|eportal> Portal login backend. Default auto.
     --portal-program-index <idx> Dr.COM Web program index override.
     --portal-page-index <n> Dr.COM Web page index override.
     --portal-js-version <ver> Dr.COM Web jsVersion override. Default 4.1.3.
     --portal-r3 <value> Dr.COM Web R3 override.
     --route-isolation <native|mwan3> How the instance is launched.
 -D, --debug [level] Enable debug output (numeric level 0-5).
 -o, --logoff
```

`--auth-method portal` runs the Web Portal backend (Dr.COM 哆点 4.x
eportal/Radius web login). The detected Location is the runtime source of
truth: it is parsed in full (scheme, host, explicit port, path, query) and
every API endpoint is derived from it — kernel `chkstatus`/`logout` follow
the Location origin, ePortal login uses the configurable ePortal port
(801 for http / 802 for https Locations, override with
`--portal-http-port` / `--portal-https-port` or the per-instance
`portal_http_port` / `portal_https_port` UCI options). The session is kept
alive with an online check every `--portal-check-interval` seconds and
re-login happens automatically when the check reports offline. All
requests are bound to the instance netdev; `--logoff` performs a portal
logout for portal instances.

### MAC helper

`/usr/lib/scutclient/scutclient-mac` manages per-instance MAC spoofing
(keep / random / custom) on wired devices or wireless STA interfaces. It never
reloads networking itself:

```sh
scutclient-mac gen                 # print a random locally administered MAC
scutclient-mac prepare <instance>  # persist MAC into network/wireless UCI,
                                   # print: none | network | wireless
scutclient-mac verify <instance>   # resolve current netdev and compare MACs
```

`/usr/lib/scutclient/scutclient-portal-probe` detects a captive portal on a
real netdev (used by the LuCI "Detect Location" button; it never follows
redirects and never changes any state):

```sh
scutclient-portal-probe <netdev>   # GET generate_204 via that device,
                                   # print http_code= and location=
```

If any advice, open an issue or contact us at SCUT Router Group.

# Contact us

SCUT Router Group (Audit) on QQ : [262939451](http://jq.qq.com/?_wv=1027&k=2EzygcA)

SCUT Router Group on [Sina Weibo](http://weibo.com/u/5148048459)

SCUT Router Podcast on [Telegram](https://t.me/joinchat/AAAAAERy9tE0gUvyTM_GrA)

# License

[AGPLv3](https://www.gnu.org/licenses/agpl-3.0.html)

![](https://www.gnu.org/graphics/agplv3-155x51.png)

We believe that you know what you are doing. You should get this software for free.
