#!/usr/bin/env bash
# W-3: compile Blink with the prototype platform via arduino-cli (symlink mode, sandboxed dirs).
# Requires: arduino-cli on PATH, CH32_GCC_BIN (riscv-none-elf-gcc bin dir)
set -euo pipefail

: "${CH32_GCC_BIN:?set CH32_GCC_BIN to the xPack riscv-none-elf-gcc bin directory}"
WORK="${1:?usage: test_compile.sh <workdir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Windows (Git Bash): arduino-cli is a native exe; paths handed to it via env
# vars or property values must be Windows-style. cygpath -m gives C:/... form.
w() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

# Sandboxed arduino-cli directories: never touches the user's real ~/.arduino15 / ~/Arduino.
export ARDUINO_DIRECTORIES_USER="$(w "$WORK/user")"
export ARDUINO_DIRECTORIES_DATA="$(w "$WORK/data")"
export ARDUINO_DIRECTORIES_DOWNLOADS="$(w "$WORK/staging")"

mkdir -p "$WORK/user/hardware/ch32-riscv-ug" "$WORK/Blink"
case "$(uname -s)" in
  MINGW*|MSYS*)  # no reliable symlinks on the Windows runner: copy instead
    rm -rf "$WORK/user/hardware/ch32-riscv-ug/ch32v"
    cp -r "$HERE/ch32v" "$WORK/user/hardware/ch32-riscv-ug/ch32v" ;;
  *)
    ln -sfn "$HERE/ch32v" "$WORK/user/hardware/ch32-riscv-ug/ch32v" ;;
esac

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

# Compile every part number listed in the generated boards.txt (compile-matrix seed).
PNUMS=$(sed -n 's/^CH32V00X\.menu\.pnum\.\([A-Z0-9]*\)=.*/\1/p' "$HERE/ch32v/boards.txt")
echo "== part numbers: $(echo "$PNUMS" | wc -w)"
: > "$WORK/sizes.tsv"

fail=0
for pnum in $PNUMS; do
  echo "== compile Blink for $pnum =="
  arduino-cli compile \
    --fqbn "ch32-riscv-ug:ch32v:CH32V00X:pnum=$pnum" \
    --build-property "compiler.path=$(w "$CH32_GCC_BIN")/" \
    --build-path "$(w "$WORK/build-$pnum")" \
    "$(w "$WORK/Blink")" || fail=1
  "$CH32_GCC_BIN/riscv-none-elf-size" "$WORK/build-$pnum/Blink.ino.elf" | \
    awk -v p="$pnum" 'NR==2 {print p "\t" $1 "\t" $2 "\t" $3}' | tee -a "$WORK/sizes.tsv"
done

# Static checks on one build: sketch's global constructor is registered in
# .init_array and crt0 contains the constructor-call loop (jalr). Runtime
# execution is verified later on hardware (HIL).
ELF="$WORK/build-CH32V006K8U7/Blink.ino.elf"
IA=$("$CH32_GCC_BIN/riscv-none-elf-size" -A "$ELF" | awk '$1==".init_array"{print $2}')
if [ -z "$IA" ] || [ "$IA" -eq 0 ]; then echo "FAIL: .init_array empty"; exit 1; fi
"$CH32_GCC_BIN/riscv-none-elf-nm" "$ELF" | grep -q "_GLOBAL__sub_I" || { echo "FAIL: no global ctor symbol"; exit 1; }
"$CH32_GCC_BIN/riscv-none-elf-objdump" -d "$ELF" | sed -n '/<handle_reset>:/,/^$/p' | grep -q "jalr" || { echo "FAIL: crt0 has no init_array call loop"; exit 1; }
echo "INIT_ARRAY CHECKS OK (.init_array=$IA bytes)"

exit $fail
