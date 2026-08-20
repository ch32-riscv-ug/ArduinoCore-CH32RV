# Point the CH32_* variables at <repo>/.tools, without disturbing any that are
# already set. Source this; do not execute it.
#
#   . "$(dirname "$0")/../../tools/index/toolenv.sh"
#
# The values come from fetch_tools.py, which reads the same tool definition
# fragments the package index is built from. Deliberately no path arithmetic
# here: the archive name differs per host in ways a case statement gets wrong
# (Windows ships a .zip, not a .tar.gz), and an earlier shell version of this
# did not even parse on macOS's bash 3.2.
#
# Nothing is reported as missing. Each script asserts what it actually needs,
# so a harness that does not use probe-rs never mentions probe-rs.

eval "$(
  "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fetch_tools.py" --print-env \
    2>/dev/null
)" || true
