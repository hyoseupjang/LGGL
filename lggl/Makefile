include $(TOPDIR)/rules.mk

PKG_NAME:=lggl
PKG_VERSION:=0.1.2
PKG_RELEASE:=2
PKG_LICENSE:=EUPL-1.2
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/lggl
  SECTION:=net
  CATEGORY:=Network
  TITLE:=LGGL Giga-lite OAM responder
  DEPENDS:=+libc
endef

define Package/lggl/description
 UB and DS Layer-2 OAM responder for explicitly selected Ethernet devices.
endef

define Package/lggl/conffiles
/etc/config/lggl
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	$(CP) ./files/LICENSE $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -std=c11 -Wall -Wextra -Werror \
		-o $(PKG_BUILD_DIR)/lggld $(PKG_BUILD_DIR)/main.c $(PKG_BUILD_DIR)/protocol.c $(TARGET_LDFLAGS)
endef

define Package/lggl/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/lggld $(1)/usr/sbin/lggld
	$(INSTALL_BIN) ./files/lggl.init $(1)/etc/init.d/lggl
	$(INSTALL_CONF) ./files/lggl.config $(1)/etc/config/lggl
	$(INSTALL_DIR) $(1)/usr/share/licenses/lggl
	$(INSTALL_DATA) ./files/LICENSE ./files/COPYRIGHT $(1)/usr/share/licenses/lggl/
endef

$(eval $(call BuildPackage,lggl))
