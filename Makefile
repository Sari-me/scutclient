include $(TOPDIR)/rules.mk

PKG_NAME:=scutclient
PKG_VERSION:=3.1.3
PKG_RELEASE:=2

PKG_MAINTAINER:=Sari-me
PKG_LICENSE:=AGPL-3.0
PKG_LICENSE_FILES:=COPYING

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1
PKG_INSTALL:=1

# 本 fork 采用 bundled source：
# 不再从 GitHub 二次下载源码，直接使用当前仓库中的 CMakeLists.txt 和 src/。
#
# 这样：
# 1. IPK 一定来自当前 Sari-me/scutclient checkout；
# 2. 不需要 PKG_HASH / PKG_MIRROR_HASH；
# 3. gh-action-sdk 的 package/check 不会再因为旧 git recipe 缺 HASH 失败。
PKG_FILE_DEPENDS:= \
	$(CURDIR)/CMakeLists.txt \
	$(CURDIR)/COPYING \
	$(wildcard $(CURDIR)/src/*.c) \
	$(wildcard $(CURDIR)/src/*.h) \
	$(CURDIR)/openwrt/files/scutclient.config \
	$(CURDIR)/openwrt/files/scutclient.init \
	$(CURDIR)/openwrt/files/scutclient.hotplug

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/scutclient
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Campus Network
  TITLE:=SCUT Dr.com client
  URL:=https://github.com/Sari-me/scutclient
endef

define Package/scutclient/description
  SCUT Dr.com/802.1X network authentication client.
endef

define Package/scutclient/conffiles
/etc/config/scutclient
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/CMakeLists.txt $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/COPYING $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/src $(PKG_BUILD_DIR)/
endef

define Package/scutclient/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) \
		$(CURDIR)/openwrt/files/scutclient.config \
		$(1)/etc/config/scutclient

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) \
		$(CURDIR)/openwrt/files/scutclient.init \
		$(1)/etc/init.d/scutclient

	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_BIN) \
		$(CURDIR)/openwrt/files/scutclient.hotplug \
		$(1)/etc/hotplug.d/iface/99-scutclient

	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) \
		$(PKG_INSTALL_DIR)/usr/bin/scutclient \
		$(1)/usr/bin/scutclient
endef

$(eval $(call BuildPackage,scutclient))