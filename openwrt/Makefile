#
# Copyright (C) 2016-2018 SCUT Router Term
#
# This is free software, licensed under the GNU Affero General Public License v3.
# See /COPYING for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=scutclient
PKG_VERSION:=3.1.3
PKG_RELEASE:=2

PKG_MAINTAINER:=Scutclient Project
PKG_LICENSE:=AGPL-3.0
PKG_LICENSE_FILES:=COPYING
PKG_BUILD_PARALLEL:=1

# GitHub Actions uses this repository itself as an OpenWrt feed.  The package
# Makefile lives in openwrt/, while the actual project source lives one level
# above it.  Copy the current checkout into PKG_BUILD_DIR so the generated IPK
# always contains the code from the commit being tested.
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/scutclient
  SECTION:=net
  CATEGORY:=Network
  TITLE:=SCUT Dr.com client
  URL:=https://github.com/scutclient/scutclient
endef

define Package/scutclient/description
 Support SCUT private authentication protocol.
endef

define Package/scutclient/conffiles
/etc/config/scutclient
endef

# Build the source code from the current GitHub checkout instead of downloading
# the old upstream v3.1.3 tag.  In gh-action-sdk, the repository is mounted at
# /feed and this package is discovered from /feed/openwrt/Makefile.
define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/../CMakeLists.txt $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/../src $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/../COPYING $(PKG_BUILD_DIR)/
endef

define Package/scutclient/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) $(CURDIR)/files/scutclient.config $(1)/etc/config/scutclient

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(CURDIR)/files/scutclient.init $(1)/etc/init.d/scutclient

	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_BIN) $(CURDIR)/files/scutclient.hotplug $(1)/etc/hotplug.d/iface/99-scutclient

	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/scutclient $(1)/usr/bin/scutclient
endef

$(eval $(call BuildPackage,scutclient))