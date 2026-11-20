#!/system/bin/sh

# Download the unmodified release directly from its publisher when needed.
NEXT_URL='https://github.com/Dr-TSNG/ZygiskNext/releases/download/v1.5.3/Zygisk-Next-1.5.3-880-22a1bf5-release.zip'
NEXT_SHA256='186a0a26c9fcb895af1954b42de831433c23d72d1bd0758b986069154359db67'
KSU='/data/adb/ksud'
MODULES='/data/adb/modules'
PENDING='/data/adb/modules_update'

prop() {
  sed -n "s/^$1=//p" "$2"
}

detect_provider() {
  provider=''
  sdk=$(getprop ro.build.version.sdk)
  magisk_version=$(magisk -V 2>/dev/null)
  manager=''
  builtin=''
  if [ -x "$KSU" ]; then
    if [ -n "$magisk_version" ]; then printf '%s\n' multiple-root; return; fi
    manager=ksu
  elif [ -n "$magisk_version" ]; then
    manager=magisk
    builtin=$(magisk --sqlite "SELECT value FROM settings WHERE key='zygisk'" 2>/dev/null) ||
      { printf '%s\n' unknown; return; }
  fi
  for id in zygisksu rezygisk neozygisk; do
    [ -d "$PENDING/$id" ] || [ -d "$MODULES/$id" ] || continue
    dir="$MODULES/$id"
    pending=''
    if [ -f "$PENDING/$id/module.prop" ]; then
      dir="$PENDING/$id"
      pending='-pending'
    fi
    if [ "$(prop id "$dir/module.prop" 2>/dev/null)" != "$id" ] ||
       [ -e "$dir/disable" ] || [ -e "$dir/remove" ] ||
       [ -e "$MODULES/$id/remove" ]; then
      printf '%s\n' disabled
      return
    fi
    if [ -n "$provider" ]; then printf '%s\n' conflict; return; fi
    provider="$id$pending"
  done
  case "$builtin" in
    1|value=1)
      if [ -n "$provider" ]; then printf '%s\n' conflict; else printf '%s\n' builtin; fi
      return ;;
    ''|0|value=0) ;;
    *) printf '%s\n' unknown; return ;;
  esac
  if [ -n "$provider" ]; then printf '%s\n' "$provider"; return; fi
  if [ -z "$manager" ]; then printf '%s\n' unsupported; return; fi
  if [ "$manager" = magisk ] && ! [ "$magisk_version" -ge 26402 ] 2>/dev/null; then
    printf '%s\n' old-magisk
    return
  fi
  if ! [ "$sdk" -ge 26 ] 2>/dev/null; then printf '%s\n' unsupported; return; fi
  printf 'ready-%s\n' "$manager"
}

install_provider() (
  state=$(detect_provider)
  case "$state" in
    builtin) echo "Zygisk tích hợp đang bật."; return 0 ;;
    *-pending) echo "Provider $state đang chờ khởi động lại."; return 0 ;;
    zygisksu|rezygisk|neozygisk) echo "Đã có provider $state."; return 0 ;;
    ready-ksu)
      if { [ -n "$1" ] && ! [ "$1" -ge 10940 ]; } ||
         { [ -n "$2" ] && ! [ "$2" -ge 11575 ]; }; then
        echo "Zygisk Next yêu cầu KernelSU 10940 và ksud 11575 trở lên." >&2
        return 1
      fi ;;
    ready-magisk) ;;
    *) echo "Zygisk Next chưa thể cài trên thiết bị này ($state)." >&2; return 1 ;;
  esac
  command -v curl >/dev/null 2>&1 ||
    { echo "Thiết bị thiếu curl để tải Zygisk Next từ GitHub." >&2; return 1; }
  lock=/dev/libtool-next.install
  mkdir "$lock" 2>/dev/null ||
    { echo "Một lượt cài Zygisk Next đang chạy." >&2; return 1; }
  trap 'rmdir "$lock"' EXIT
  trap 'exit 1' HUP INT TERM
  work=$(mktemp -d /data/local/tmp/libtool-next.XXXXXX) || return 1
  zip="$work/zygisk-next.zip"
  trap 'rm -f "$zip"; rmdir "$work" "$lock"' EXIT
  echo "Đang tải Zygisk Next 1.5.3 từ bản phát hành chính thức…"
  if ! curl --fail --silent --show-error --location --proto '=https' \
      --connect-timeout 15 --max-time 90 --output "$zip" "$NEXT_URL"; then
    echo "Không tải được Zygisk Next. Có thể thử lại trong giao diện quản lý." >&2
    return 1
  fi
  if ! printf '%s  %s\n' "$NEXT_SHA256" "$zip" | sha256sum -c -; then
    echo "SHA-256 của ZIP Zygisk Next không khớp." >&2
    return 1
  fi
  # Recheck after download in case another installer added a provider.
  [ "$(detect_provider)" = "$state" ] ||
    { echo "Trạng thái Zygisk đã thay đổi. Tải lại trang để kiểm tra." >&2; return 1; }
  cd / || return 1
  case "$state" in
    ready-ksu) env -i PATH="$PATH" BOOTMODE=true "$KSU" module install "$zip" || return 1 ;;
    ready-magisk) env -i PATH="$PATH" BOOTMODE=true magisk --install-module "$zip" || return 1 ;;
  esac
  case "$(detect_provider)" in
    zygisksu|zygisksu-pending) echo "Đã cài Zygisk Next. Khởi động lại để áp dụng." ;;
    *) echo "Trình quản lý chưa ghi nhận module Zygisk Next." >&2; return 1 ;;
  esac
)

case "$1" in
  status) detect_provider ;;
  install) install_provider "${2:-}" "${3:-}" ;;
  *) echo "Usage: $0 status|install" >&2; exit 2 ;;
esac
