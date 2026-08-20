#!/usr/bin/env bash
# W-5: clean Board Manager install via local HTTP (R-15 mode B), then compile Blink
# WITHOUT any compiler.path override (the installed tool must resolve).
# Requires: arduino-cli on PATH, CH32_XPACK_ARCHIVE (local copy of this host's
# xPack tar.gz, so the test does not re-download 400MB from GitHub).
# Optional: CH32_PROBE_RS_ARCHIVE, the same trick for probe-rs. Without it the
# probe-rs entry keeps pointing at GitHub and the install downloads it, which is
# what CI does; with it the whole install runs offline.
set -euo pipefail

: "${CH32_XPACK_ARCHIVE:?set CH32_XPACK_ARCHIVE to a local xPack 14.3.0-1 archive for this host}"
WORK="${1:?usage: test_install.sh <workdir>}"
PORT="${PORT:-8731}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BASE_URL="http://127.0.0.1:$PORT"

# Windows (Git Bash) compatibility:
# native tools (arduino-cli, python) need Windows-style paths (cygpath -m).
PY="uv run --no-project python"
w() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

mkdir -p "$WORK/www" "$WORK/Blink"

# 1) package the platform and generate the index (tool URL -> local server)
LOCAL_TOOLS="xpack-riscv-none-elf-gcc"
[ -n "${CH32_PROBE_RS_ARCHIVE:-}" ] && LOCAL_TOOLS="$LOCAL_TOOLS,probe-rs"
$PY "$HERE/gen_index.py" --platform "$(w "$HERE/../..")" \
  --out "$(w "$WORK/www")" --base-url "$BASE_URL" --tools local \
  --local-tools "$LOCAL_TOOLS"

# Serve the provided archive under the name the index expects for it. The entry
# is found by SHA-256, so a wrong/corrupt archive fails here instead of later.
ARCHIVE_NAME=$($PY - "$CH32_XPACK_ARCHIVE" "$HERE/tools_xpack_gcc.json" <<'EOF'
import hashlib, json, sys
h = hashlib.sha256()
with open(sys.argv[1], "rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
        h.update(chunk)
frag = json.load(open(sys.argv[2]))
for s in frag["systems"]:
    if s["checksum"] == "SHA-256:" + h.hexdigest():
        print(s["archiveFileName"]); break
else:
    sys.exit("archive does not match any checksum in tools_xpack_gcc.json")
EOF
)
cp "$CH32_XPACK_ARCHIVE" "$WORK/www/$ARCHIVE_NAME"

if [ -n "${CH32_PROBE_RS_ARCHIVE:-}" ]; then
  PROBE_NAME=$($PY - "$CH32_PROBE_RS_ARCHIVE" "$HERE/tools_probe_rs.json" <<'EOF'
import hashlib, json, sys
h = hashlib.sha256()
with open(sys.argv[1], "rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
        h.update(chunk)
frag = json.load(open(sys.argv[2]))
for s in frag["systems"]:
    if s["checksum"] == "SHA-256:" + h.hexdigest():
        print(s["archiveFileName"]); break
else:
    sys.exit("archive does not match any checksum in tools_probe_rs.json")
EOF
)
  cp "$CH32_PROBE_RS_ARCHIVE" "$WORK/www/$PROBE_NAME"
fi

# 2) serve it
$PY -m http.server "$PORT" --bind 127.0.0.1 --directory "$(w "$WORK/www")" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
sleep 1

# 3) clean install into sandboxed directories (fresh data dir = real user path)
export ARDUINO_DIRECTORIES_USER="$(w "$WORK/user")"
export ARDUINO_DIRECTORIES_DATA="$(w "$WORK/data")"
export ARDUINO_DIRECTORIES_DOWNLOADS="$(w "$WORK/staging")"
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
  --fqbn "ch32-riscv-ug:ch32v:CH32V006:pnum=ANY" \
  --build-path "$(w "$WORK/build")" \
  "$(w "$WORK/Blink")"

# ...and the real acceptance sketch on the tier A board, because Blink touches
# neither Serial nor the heap - the two places where an installed platform has
# actually differed from the working tree.
mkdir -p "$WORK/Acceptance"
cp "$HERE/../../tests/sketches/basic/serial_println/serial_println.ino" \
   "$WORK/Acceptance/Acceptance.ino"
arduino-cli compile \
  --fqbn "ch32-riscv-ug:ch32v:CH32X035:pnum=ANY" \
  --build-path "$(w "$WORK/build2")" \
  "$(w "$WORK/Acceptance")"

# The archive ships to users: it must not carry the repository's own
# scaffolding. gen_index.py packages an allowlist, so a leak here means the
# allowlist grew something it should not have.
INSTALLED="$WORK/data/packages/ch32-riscv-ug/hardware/ch32v"
for junk in tests docs tools vendor .git .github; do
  found=$(find "$INSTALLED" -maxdepth 2 -name "$junk" 2>/dev/null | head -1)
  [ -z "$found" ] || { echo "FAIL: release archive contains $found"; exit 1; }
done
echo "ARCHIVE CONTENTS OK"

# The upload path is only real if the programmer tool came down with the
# platform: probe-rs ships as .tar.xz, which not every archive reader handles.
PROBE=$(find "$WORK/data/packages/ch32-riscv-ug/tools/probe-rs" -name "probe-rs*" -type f 2>/dev/null | head -1)
[ -n "$PROBE" ] || { echo "FAIL: probe-rs was not installed with the platform"; exit 1; }
"$PROBE" --version || { echo "FAIL: installed probe-rs does not run"; exit 1; }
echo "PROBE-RS INSTALL OK: $PROBE"

# 5) upgrade path. A Board Manager index is append-only, and users upgrade in
# place rather than reinstalling; regenerating the index from scratch used to
# drop every older version, which is invisible until someone pins one.
# Stage only the release entries: gen_index.py reads nothing else, and copying
# the repository root would also copy this work directory when it lives inside.
UP="$WORK/upgrade"
mkdir -p "$UP/platform"
for e in platform.txt boards.txt programmers.txt cores variants libraries bootloaders system; do
  [ -e "$HERE/../../$e" ] && cp -r "$HERE/../../$e" "$UP/platform/"
done
NEXT="0.0.$(( $($PY -c "import re,sys;print(re.search(r'^version=(\d+)\.(\d+)\.(\d+)',open(sys.argv[1]).read(),re.M).group(3))" "$UP/platform/platform.txt") + 1 ))"
$PY - "$UP/platform/platform.txt" "$NEXT" <<'EOF'
import re, sys
p = sys.argv[1]
t = open(p).read()
open(p, "w").write(re.sub(r"^version=.*$", "version=" + sys.argv[2], t, count=1, flags=re.M))
EOF
$PY "$HERE/gen_index.py" --platform "$(w "$UP/platform")"   --out "$(w "$WORK/www")" --base-url "$BASE_URL" --tools local   --merge "$(w "$WORK/www/package_ch32-riscv-ug_index.json")"

arduino-cli core update-index --additional-urls "$INDEX_URL"
arduino-cli core upgrade "ch32-riscv-ug:ch32v" --additional-urls "$INDEX_URL"
arduino-cli core list | grep -q "$NEXT" || {
  echo "FAIL: upgrade to $NEXT did not take"; arduino-cli core list; exit 1; }
# The older version must still be installable from the same index.
arduino-cli core install "ch32-riscv-ug:ch32v@0.0.1" --additional-urls "$INDEX_URL"
arduino-cli core list | grep -q "0.0.1" || {
  echo "FAIL: index is not append-only, 0.0.1 is gone"; exit 1; }
echo "UPGRADE AND ROLLBACK OK ($NEXT then back to 0.0.1)"

echo "INSTALL-AND-COMPILE OK"
