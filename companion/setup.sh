#!/usr/bin/env bash
# CFace companion setup for macOS/Linux. Run from anywhere; works from the clone.
#   ./setup.sh [host[:port]]     (--dry-run prints actions only)
set -euo pipefail
DRY=0; HOST=""
for a in "$@"; do case "$a" in --dry-run) DRY=1;; *) HOST="$a";; esac; done
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PY=$(command -v python3 || command -v python || true)
[ -n "$PY" ] || { echo "Python 3 not found. Install it from https://www.python.org/downloads/ and re-run." >&2; exit 1; }
"$PY" -c 'import sys; sys.exit(sys.version_info < (3,8))' || { echo "Python 3.8+ required." >&2; exit 1; }

if [ -z "$HOST" ]; then read -r -p "ESP32 IP or host shown on the OLED (add :port if not 80): " HOST; fi
HOST="${HOST#http://}"; HOST="${HOST%%/*}"
[ -n "$HOST" ] || { echo "No host given." >&2; exit 1; }

URL="http://$HOST/notify"
CMD="$PY \"$DIR/notify-oled.py\""
echo "Config:  $DIR/config.env  (ESP32_FACE_URL=$URL)"
echo "Hook:    $CMD"
if [ "$DRY" = 1 ]; then echo "(dry run, nothing written)"; exit 0; fi

sed "s|^ESP32_FACE_URL=.*|ESP32_FACE_URL=$URL|" "$DIR/config.env.example" > "$DIR/config.env"
"$PY" "$DIR/notify-oled.py" --text "Setup OK" --verbose || echo "Could not reach the board. Is this machine on the same network? Setup continues." >&2
"$PY" "$DIR/merge-hook.py" --hooks "$HOME/.cursor/hooks.json" --command "$CMD"
echo "Done. Restart Cursor so it loads the hook."
