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

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/scutclient
  SECTION:=net
  CATEGORY:=Network
  TITLE:=SCUT Dr.com client
  URL:=https://github.com/Sari-me/scutclient
endef

define Package/scutclient/description
 SCUT Dr.com/802.1X network authentication client.
endef

define Package/scutclient/conffiles
/etc/config/scutclient
endef

#
# The GitHub Actions workflow constructs:
#
#   .ci-feed/scutclient/
#     Makefile
#     CMakeLists.txt
#     COPYING
#     src/
#     files/
#
# gh-action-sdk mounts .ci-feed as /feed, therefore all bundled source
# files are available directly below $(CURDIR).
#
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
		$(CURDIR)/files/scutclient.config \
		$(1)/etc/config/scutclient

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) \
		$(CURDIR)/files/scutclient.init \
		$(1)/etc/init.d/scutclient

	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_BIN) \
		$(CURDIR)/files/scutclient.hotplug \
		$(1)/etc/hotplug.d/iface/99-scutclient

	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) \
		$(PKG_BUILD_DIR)/scutclient \
		$(1)/usr/bin/scutclient
endef

$(eval $(call BuildPackage,scutclient))