include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-lggl
PKG_VERSION:=0.1.2
PKG_RELEASE:=3
PKG_LICENSE:=EUPL-1.2

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-lggl
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI interface for LGGL
  DEPENDS:=+lggl +luci-base +rpcd +jshn
  PKGARCH:=all
endef

define Build/Compile
endef

define Package/luci-app-lggl/install
	$(INSTALL_DIR) $(1)/www
	$(CP) ./htdocs/* $(1)/www/
	$(CP) ./root/* $(1)/
endef

define Package/luci-app-lggl/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -f /tmp/luci-indexcache.*
	rm -rf /tmp/luci-modulecache/
	/etc/init.d/rpcd reload 2>/dev/null || true
fi
exit 0
endef

$(eval $(call BuildPackage,luci-app-lggl))
