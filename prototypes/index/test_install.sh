#!/usr/bin/env bash
# W-5: clean Board Manager install via local HTTP (R-15 mode B), then compile Blink
# WITHOUT any compiler.path override (the installed tool must resolve).
# Requires: arduino-cli on PATH, CH32_XPACK_ARCHIVE (local copy of the linux-x64
# xPack tar.gz, so the test does not re-download 400MB from GitHub).
set -euo pipefail

: "${CH32_XPACK_ARCHIVE:?set CH32_XPACK_ARCHIVE to the local xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz}"
WORK="${1:?usage: test_install.sh <workdir>}"
PORT="${PORT:-8731}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BASE_URL="http://127.0.0.1:$PORT"

mkdir -p "$WORK/www" "$WORK/Blink"

# 1) package the platform and generate the index (tool URL -> local server)
python3 "$HERE/gen_index.py" --platform "$HERE/../platform/ch32v" \
  --out "$WORK/www" --base-url "$BASE_URL" --tools local
cp "$CH32_XPACK_ARCHIVE" "$WORK/www/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz"

# 2) serve it
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$WORK/www" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
sleep 1

# 3) clean install into sandboxed directories (fresh data dir = real user path)
export ARDUINO_DIRECTORIES_USER="$WORK/user"
export ARDUINO_DIRECTORIES_DATA="$WORK/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$WORK/staging"
INDEX_URL="$BASE_URL/package_ch32-riscv-ug_index.json"

arduino-cli core update-index --additional-urls "$INDEX_URL"
arduino-cli core install "ch32-riscv-ug:ch32v" --additional-urls "$INDEX_URL"
arduino-cli core list

# 4) compile without any override: toolchain must come from the installed tool
cat > "$WORK/Blink/Blink.ino" <<'EOF'
void setup() { pinMode(LED_BUILTIN, OUTPUT); }
void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
EOF
arduino-cli compile \
  --fqbn "ch32-riscv-ug:ch32v:CH32V00X:pnum=CH32V006K8U7" \
  --build-path "$WORK/build" \
  "$WORK/Blink"

echo "INSTALL-AND-COMPILE OK"
