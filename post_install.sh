#!/bin/sh
# Installs ch32rv's udev rules so a WCH-Link, an OEP probe's bootloader and the HID bootloaders
# open without root (ArduinoCore-CH32RV request B-6, decided 2026-09-02). The Arduino IDE 2.x runs
# this after installing the platform on Linux; arduino-cli runs it only in an interactive terminal
# (or with --run-post-install); the IDE 1.x never does.
#
# 60-ch32rv.rules next to this script is a byte copy of what the bundled ch32rv prints with
# `ch32rv doctor --emit-udev` (tests/build/vendor/test_udev_rules.py keeps them in step).
#
# The IDE runs this as the ordinary user, so /etc/udev is usually not writable: then say what to
# type and exit 0 - a failed post-install must not make the platform install fail. `ch32rv doctor`
# is the last resort: it notices the missing rule and says the same.
set -u
RULES="$(dirname "$0")/60-ch32rv.rules"
DEST=/etc/udev/rules.d/60-ch32rv.rules

[ "$(uname -s)" = "Linux" ] || exit 0
[ -f "$RULES" ] || { echo "ArduinoCore-CH32RV: $RULES is missing; run: ch32rv doctor --emit-udev | sudo tee $DEST"; exit 0; }

if [ -f "$DEST" ] && cmp -s "$RULES" "$DEST"; then
    exit 0
fi
if cp "$RULES" "$DEST" 2>/dev/null; then
    udevadm control --reload-rules 2>/dev/null && udevadm trigger 2>/dev/null
    echo "ArduinoCore-CH32RV: installed $DEST"
    exit 0
fi
cat <<EOF
ArduinoCore-CH32RV: the udev rules for the WCH-Link / OEP probes were not installed (no permission
to write $DEST). To let the probes open without root, run once:

    sudo cp "$RULES" $DEST
    sudo udevadm control --reload-rules && sudo udevadm trigger

then unplug and replug the probe. \`ch32rv doctor\` checks this too.
EOF
exit 0
