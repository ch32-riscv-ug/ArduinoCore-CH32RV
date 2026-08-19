#!/usr/bin/env bash
# W-3: compile Blink with the prototype platform via arduino-cli (symlink mode, sandboxed dirs).
# Requires: arduino-cli on PATH, CH32_GCC_BIN (riscv-none-elf-gcc bin dir)
set -euo pipefail

: "${CH32_GCC_BIN:?set CH32_GCC_BIN to the xPack riscv-none-elf-gcc bin directory}"
WORK="${1:?usage: test_compile.sh <workdir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Sandboxed arduino-cli directories: never touches the user's real ~/.arduino15 / ~/Arduino.
export ARDUINO_DIRECTORIES_USER="$WORK/user"
export ARDUINO_DIRECTORIES_DATA="$WORK/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$WORK/staging"

mkdir -p "$WORK/user/hardware/ch32-riscv-ug" "$WORK/Blink"
ln -sfn "$HERE/ch32v" "$WORK/user/hardware/ch32-riscv-ug/ch32v"

cat > "$WORK/Blink/Blink.ino" <<'EOF'
// Global constructor to exercise C++ compilation (NOTE: not yet executed at
// runtime - crt0 does not call .init_array yet, see prototypes/platform/README.ja.md)
struct Marker { int v; Marker() : v(42) {} };
Marker marker;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
EOF

echo "== boards =="
arduino-cli board listall

fail=0
for pnum in CH32V006K8U7 CH32V002F4U6; do
  echo "== compile Blink for $pnum =="
  arduino-cli compile \
    --fqbn "ch32-riscv-ug:ch32v:CH32V00X:pnum=$pnum" \
    --build-property "compiler.path=$CH32_GCC_BIN/" \
    --build-path "$WORK/build-$pnum" \
    "$WORK/Blink" || fail=1
  "$CH32_GCC_BIN/riscv-none-elf-size" "$WORK/build-$pnum/Blink.ino.elf"
done
exit $fail
