#!/usr/bin/env bash
# Every sketch must compile for every board its own sketch.yaml lists.
#
# The profiles are a promise: `pytest --profile <p>` will build and flash that
# sketch on that board. Nothing checked the promise, and it broke immediately -
# a global String does not fit CH32V003's 2 KB of RAM, so three sketches listed
# a board they could never link for. This runs the whole grid without hardware.
#
# Board-specific limits belong in REQUIREMENTS in sync_profiles.py, not here:
# this script only reports what the generated profiles claim.
#
# Requires: arduino-cli on PATH, CH32_GCC_BIN (riscv-none-elf-gcc bin dir)
set -euo pipefail

: "${CH32_GCC_BIN:?set CH32_GCC_BIN to the xPack riscv-none-elf-gcc bin directory}"
WORK="${1:?usage: compile_all.sh <workdir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"
PLATFORM="$(cd "$HERE/../.." && pwd)"

w() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

export ARDUINO_DIRECTORIES_USER="$(w "$WORK/user")"
export ARDUINO_DIRECTORIES_DATA="$(w "$WORK/data")"
export ARDUINO_DIRECTORIES_DOWNLOADS="$(w "$WORK/staging")"

mkdir -p "$WORK/user/hardware/ch32-riscv-ug"
case "$(uname -s)" in
  MINGW*|MSYS*)
    rm -rf "$WORK/user/hardware/ch32-riscv-ug/ch32v"
    mkdir -p "$WORK/user/hardware/ch32-riscv-ug/ch32v"
    for e in platform.txt boards.txt programmers.txt cores variants libraries; do
      [ -e "$PLATFORM/$e" ] && cp -r "$PLATFORM/$e" "$WORK/user/hardware/ch32-riscv-ug/ch32v/"
    done ;;
  *)
    ln -sfn "$PLATFORM" "$WORK/user/hardware/ch32-riscv-ug/ch32v" ;;
esac

fail=0
count=0
for yaml in "$HERE"/*/*/sketch.yaml; do
  src="$(dirname "$yaml")"
  name="$(basename "$src")"
  # Copy the .ino out from under its sketch.yaml: with the profile file present
  # arduino-cli resolves the platform through platform_index_url and ignores
  # --fqbn, so the symlinked working tree is never built. The copy also proves
  # the sketch needs nothing else from its directory.
  dir="$WORK/sketches/$name"
  mkdir -p "$dir"
  cp "$src/$name.ino" "$dir/"
  # `fqbn:` lines carry the board; the profile name is only a label.
  while read -r fqbn; do
    board="$(echo "$fqbn" | cut -d: -f3)"
    count=$((count + 1))
    printf '== %-16s %-10s ' "$name" "$board"
    if arduino-cli compile --fqbn "$fqbn" \
         --build-property "compiler.path=$(w "$CH32_GCC_BIN")/" \
         --build-path "$(w "$WORK/build")" "$(w "$dir")" \
         > "$WORK/last.log" 2>&1; then
      grep -m1 -o 'Sketch uses [0-9]* bytes ([0-9]*%)' "$WORK/last.log" || echo ok
    else
      echo "FAIL"
      sed 's/^/     /' "$WORK/last.log" | tail -12
      fail=$((fail + 1))
    fi
    rm -rf "$WORK/build"
  done < <(grep -oE 'ch32-riscv-ug:ch32v:[A-Za-z0-9]+:pnum=[A-Za-z0-9]+' "$yaml" | sort -u)
done

echo "---"
if [ "$fail" -ne 0 ]; then
  echo "SKETCH PROFILE COMPILE FAILED: $fail of $count"
  exit 1
fi
echo "SKETCH PROFILE COMPILE OK: $count combinations"
