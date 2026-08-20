# Fill in the tool locations that are not already set, from <repo>/.tools.
# Source this; do not execute it.
#
#   . "$(git rev-parse --show-toplevel)/tools/index/toolenv.sh"
#
# Anything already exported wins, so CI or a bench with its own copy can point
# elsewhere without editing scripts. Only the unset ones get a default, and a
# default that does not exist yet is reported with the command that fixes it
# rather than failing later inside a compiler invocation.

_toolenv_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

_toolenv_missing=""

_toolenv_set() {   # <var> <path> <what>
  local var="$1" path="$2" what="$3"
  if [ -n "${!var:-}" ]; then
    return
  fi
  if [ -e "$path" ]; then
    export "$var=$path"
  else
    _toolenv_missing="${_toolenv_missing}  ${what} (${var})\n"
  fi
}

# Versions come from the tool definition fragments, so this stays correct when
# a version is bumped there.
_toolenv_gcc_version="$(sed -n 's/.*"version": "\(.*\)".*/\1/p' \
  "$_toolenv_repo/tools/index/tools_xpack_gcc.json" | head -1)"
_toolenv_probe_version="$(sed -n 's/.*"version": "\(.*\)".*/\1/p' \
  "$_toolenv_repo/tools/index/tools_probe_rs.json" | head -1)"

_toolenv_set CH32_GCC_BIN \
  "$_toolenv_repo/.tools/xpack-riscv-none-elf-gcc/$_toolenv_gcc_version/bin" \
  "RISC-V toolchain"
_toolenv_set CH32_PROBE_RS \
  "$_toolenv_repo/.tools/probe-rs/$_toolenv_probe_version" \
  "probe-rs"
_toolenv_set CH32_TABLES \
  "$_toolenv_repo/.tools/ch32-device-data/tables" \
  "ch32-device-data tables"
# test_install.sh serves this archive locally instead of pulling 400 MB from
# GitHub on every run.
_toolenv_set CH32_XPACK_ARCHIVE \
  "$_toolenv_repo/.tools/cache/xpack-riscv-none-elf-gcc-$_toolenv_gcc_version-$(
     case "$(uname -s)/$(uname -m)" in
       Linux/x86_64)  echo linux-x64 ;;
       Linux/aarch64) echo linux-arm64 ;;
       Darwin/x86_64) echo darwin-x64 ;;
       Darwin/arm64)  echo darwin-arm64 ;;
       *)             echo unknown ;;
     esac).tar.gz" \
  "xPack archive"

if [ -n "$_toolenv_missing" ]; then
  printf 'missing from %s/.tools:\n' "$_toolenv_repo" >&2
  printf "$_toolenv_missing" >&2
  printf 'run: uv run tools/index/fetch_tools.py\n' >&2
fi

unset _toolenv_set _toolenv_repo _toolenv_gcc_version _toolenv_probe_version
