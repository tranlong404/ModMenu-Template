#!/system/bin/sh
MODDIR=${0%/*}
ID=zygisk_modmenu_template
NAME='Mod Menu Template'
HOST=io.github.a13e300.ksuwebui
URI="kernelsu://webui?id=$ID"
for manager in me.weishu.kernelsu com.rifsxd.ksunext com.sukisu.ultra me.bmax.apatch; do
  pm path "$manager" 2>/dev/null | grep -q '^package:' || continue
  for activity in me.weishu.kernelsu.ui.webui.WebUIActivity "$manager.ui.webui.WebUIActivity"; do
    result=$(am start -a android.intent.action.VIEW -n "$manager/$activity" -d "$URI" \
      --es id "$ID" --es moduleId "$ID" --es name "$NAME" 2>&1)
    case "$result" in
      *'Error'*|*'Exception'*) continue ;;
      *) printf '%s\n' "$result"; exit 0 ;;
    esac
  done
done

APK="$MODDIR/webui/KsuWebUI.apk"
if ! pm path "$HOST" >/dev/null 2>&1 && [ -f "$APK" ]; then
  (cd "$MODDIR" && grep '  webui/KsuWebUI.apk$' payload.sha256 | sha256sum -c -) &&
    pm install -S "$(wc -c < "$APK")" < "$APK"
fi
if pm path "$HOST" >/dev/null 2>&1; then
  am start -n "$HOST/.WebUIActivity" --es id "$ID" --es name "$NAME" && exit 0
fi
echo 'Open WebUI from KernelSU or use KsuWebUI Standalone with root to configure Mod Menu Template.'
exit 1
