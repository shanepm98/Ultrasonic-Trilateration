#!/bin/sh
# Build this app's firmware inside the espressif/idf Docker image.
#
# Mounts the repo's src/ directory (not just this app) so the shared components under
# src/components/ (invn-soniclib, soniclib_esp32_bsp, icu_post) and the pitch-catch common
# header under ../include/ resolve.
#
# Pass extra idf.py args through, e.g.:  ./build.sh flash monitor
# Set PORT to the serial device (default /dev/ttyUSB0); passed into the container only if present.
set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src_dir=$(CDPATH= cd -- "$script_dir/../../.." && pwd)   # .../src
app_rel=${script_dir#"$src_dir"/}                         # apps/pitch_catch_mode_hardware_test/sender
port="${PORT:-/dev/ttyUSB0}"

if [ "$#" -eq 0 ]; then
	set -- build
fi

device_args=""
if [ -e "$port" ]; then
	# Pass the serial device in, plus its group (uucp/dialout - gid varies by distro) as a
	# supplementary group so the non-root container user can open it.
	port_gid=$(stat -c '%g' "$port" 2>/dev/null || echo "")
	device_args="--device=$port"
	[ -n "$port_gid" ] && device_args="$device_args --group-add=$port_gid"
fi

exec docker run --rm \
	-v "$src_dir":/src \
	-w "/src/$app_rel" \
	-u "$(id -u)" \
	-e HOME=/tmp \
	$device_args \
	espressif/idf idf.py "$@"
