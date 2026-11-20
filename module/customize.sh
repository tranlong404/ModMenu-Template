#!/system/bin/sh
SKIPUNZIP=1
[ "$BOOTMODE" = true ] || abort "Install through KernelSU/Magisk."
case "$ARCH" in arm64|aarch64|x64|x86_64) ;; *) abort "Requires a 64-bit Android instance." ;; esac
SAVED_TARGETS="${TMPDIR:-/data/local/tmp}/modmenu-targets.$$"
SAVED_DELAY="${TMPDIR:-/data/local/tmp}/modmenu-delay.$$"
if [ -f /data/adb/modules/zygisk_modmenu_template/targets.txt ]; then
  cp /data/adb/modules/zygisk_modmenu_template/targets.txt "$SAVED_TARGETS" || true
fi
if [ -f /data/adb/modules/zygisk_modmenu_template/delay.txt ]; then
  cp /data/adb/modules/zygisk_modmenu_template/delay.txt "$SAVED_DELAY" || true
fi
unzip -o "$ZIPFILE" -x 'META-INF/*' -d "$MODPATH" >&2 || abort "Payload extraction failed."
(cd "$MODPATH" && sha256sum -c payload.sha256) || abort "Checksum failure."
[ -f "$SAVED_TARGETS" ] && cp "$SAVED_TARGETS" "$MODPATH/targets.txt" && rm -f "$SAVED_TARGETS"
[ -f "$SAVED_DELAY" ] && cp "$SAVED_DELAY" "$MODPATH/delay.txt" && rm -f "$SAVED_DELAY"
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644
set_perm_recursive "$MODPATH/payload" 0 0 0755 0644
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644
set_perm_recursive "$MODPATH/webui" 0 0 0755 0644
set_perm "$MODPATH/webui/app-labels.dex" 0 0 0444
set_perm "$MODPATH/configure.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/zygisk-provider.sh" 0 0 0755
set_perm "$MODPATH/targets.txt" 0 0 0644
set_perm "$MODPATH/delay.txt" 0 0 0644
ui_print "- Mod Menu Template installed. Reboot and select target app in WebUI."
