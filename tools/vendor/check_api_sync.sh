#!/usr/bin/env bash
# Verify cores/arduino/api matches the upstream ArduinoCore-API commit pinned in
# vendor/arduino-core-api.lock.toml, byte for byte.
# See docs/adr/0009-arduinocore-api-import.ja.md
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
LOCK="$REPO/vendor/arduino-core-api.lock.toml"
DEST="$REPO/cores/arduino/api"
WORK="${1:-$(mktemp -d)}"

val() { sed -n "s/^$1 = \"\\(.*\\)\".*/\\1/p" "$LOCK"; }
URL=$(val url); TAG=$(val tag); COMMIT=$(val commit)
TREE=$(sed -n 's/^api_tree_sha1 = "\([0-9a-f]*\)".*/\1/p' "$LOCK")
APIVER=$(sed -n 's/^arduino_api_version = \([0-9]*\).*/\1/p' "$LOCK")
echo "pinned: $URL @ $TAG ($COMMIT), api tree $TREE"

mkdir -p "$WORK"
UP="$WORK/ArduinoCore-API"
if [ ! -d "$UP/.git" ]; then
  git clone --quiet --filter=blob:none --no-checkout "$URL" "$UP"
fi
git -C "$UP" fetch --quiet --depth 1 origin "$COMMIT"
git -C "$UP" checkout --quiet --detach "$COMMIT"

# 1) the pinned commit really carries the pinned api/ tree
ACTUAL_TREE=$(git -C "$UP" rev-parse "$COMMIT^{tree}:api")
[ "$ACTUAL_TREE" = "$TREE" ] || { echo "FAIL: upstream api/ tree $ACTUAL_TREE != pinned $TREE"; exit 1; }

# 2) our copy is byte-identical to upstream api/ (LICENSE is upstream's root LICENSE)
cp "$UP/LICENSE" "$UP/api/LICENSE"
if ! diff -ru "$UP/api" "$DEST"; then
  echo "FAIL: cores/arduino/api differs from upstream $TAG"; exit 1
fi

# 3) every file is listed in the lock with a matching SHA-256
rc=0
while IFS= read -r line; do
  path=$(echo "$line" | sed -n 's/.*path = "\([^"]*\)".*/\1/p')
  want=$(echo "$line" | sed -n 's/.*sha256 = "\([^"]*\)".*/\1/p')
  got=$(sha256sum "$DEST/$path" 2>/dev/null | cut -d' ' -f1) || true
  [ "$got" = "$want" ] || { echo "FAIL: sha256 mismatch $path"; rc=1; }
done < <(grep '{ path = ' "$LOCK")
listed=$(grep -c '{ path = ' "$LOCK")
present=$(cd "$DEST" && find . -type f | wc -l)
[ "$listed" -eq "$present" ] || { echo "FAIL: lock lists $listed files, tree has $present"; rc=1; }

# 4) ARDUINO_API_VERSION guard matches the pin
grep -q "^#define ARDUINO_API_VERSION $APIVER\$" "$DEST/ArduinoAPI.h" || {
  echo "FAIL: ArduinoAPI.h does not define ARDUINO_API_VERSION $APIVER"; rc=1; }

[ $rc -eq 0 ] && echo "API SYNC OK ($listed files, $TAG, ARDUINO_API_VERSION $APIVER)"
exit $rc
