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
/etc/init.d/scutclient start_instance  <id>
/etc/init.d/scutclient stop_instance   <id>
/etc/init.d/scutclient restart_instance <id>
/etc/init.d/scutclient logoff_instance <id>
```

One WAN (logical interface, or the resolved Linux netdev) can only be bound by
one instance; the init script refuses conflicting instances at start-up.

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
 -D, --debug [level] Enable debug output (numeric level 0-5).
 -o, --logoff
```

### MAC helper

`/usr/lib/scutclient/scutclient-mac` manages per-instance MAC spoofing
(keep / random / custom) on wired devices or wireless STA interfaces:

```sh
scutclient-mac apply <instance>   # apply configured MAC, print effective one
scutclient-mac gen                # print a random locally administered MAC
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
