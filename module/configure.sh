#!/system/bin/sh
MODULE=$(dirname "$0")
[ "$#" -gt 0 ] || { cat "$MODULE/targets.txt"; exit 0; }
if [ "$#" = 1 ] && [ "$1" = --get-delay ]; then
  if [ -f "$MODULE/delay.txt" ]; then
    VAL=$(head -n 1 "$MODULE/delay.txt" 2>/dev/null | tr -cd '0-9')
    if [ -n "$VAL" ] && [ "$VAL" -le 180 ]; then
      printf '%s\n' "$VAL"
      exit 0
    fi
  fi
  printf '3\n'
  exit 0
fi
if [ "$#" = 2 ] && [ "$1" = --delay ]; then
  VAL="$2"
  case "$VAL" in *[!0-9]*|'') exit 1;; esac
  [ "$VAL" -le 180 ] || exit 1
  TMP=$(mktemp "$MODULE/delay.pending.XXXXXX") || exit 1
  trap 'rm -f "$TMP"' EXIT
  trap 'exit 1' HUP INT TERM
  printf '%s\n' "$VAL" > "$TMP" || exit 1
  chmod 0644 "$TMP" && mv -f "$TMP" "$MODULE/delay.txt"
  exit $?
fi
TMP=$(mktemp "$MODULE/targets.pending.XXXXXX") || exit 1
trap 'rm -f "$TMP"' EXIT
trap 'exit 1' HUP INT TERM
if [ "$#" = 1 ] && [ "$1" = --clear ]; then
  chmod 0644 "$TMP" && mv -f "$TMP" "$MODULE/targets.txt"
  exit $?
fi
for name in "$@"; do
 case "$name" in *[!a-zA-Z0-9_.:]*|'') exit 1;; esac
 printf '%s\n' "$name" >> "$TMP" || exit 1
done
chmod 0644 "$TMP" && mv -f "$TMP" "$MODULE/targets.txt"
