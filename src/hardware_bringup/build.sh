#!/bin/sh
# Build the hardware bring-up POST firmware inside the espressif/idf Docker image.
#
# Unlike src/soniclib_esp32_bsp/build.sh this mounts the repo's whole src/ directory (not just
# this project) so the shared soniclib_esp32_bsp BSP and invn-soniclib component resolve.
#
# Pass extra idf.py args through, e.g.:  ./build.sh flash monitor
# Set PORT to the serial device to flash/monitor (default /dev/ttyUSB0); it is passed into the
# container only if it exists.
set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src_dir=$(dirname -- "$script_dir")
port="${PORT:-/dev/ttyUSB0}"

if [ "$#" -eq 0 ]; then
	set -- build
fi

device_args=""
if [ -e "$port" ]; then
	# Pass the serial device in, and add its group (e.g. uucp/dialout, gid varies by distro)
	# as a supplementary group so the non-root container user can open it.
	port_gid=$(stat -c '%g' "$port" 2>/dev/null || echo "")
	device_args="--device=$port"
	[ -n "$port_gid" ] && device_args="$device_args --group-add=$port_gid"
fi

exec docker run --rm \
	-v "$src_dir":/src \
	-w /src/hardware_bringup \
	-u "$(id -u)" \
	-e HOME=/tmp \
	$device_args \
	espressif/idf idf.py "$@"
