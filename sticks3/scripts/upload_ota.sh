#!/bin/sh
# Build the production image and upload it to the original StickS3 PTT OTA
# identity. The password is read from the existing ignored secrets header and
# is never printed or copied into this repository.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(dirname "$script_dir")
repo_dir=$(dirname "$project_dir")

if [ -f "$repo_dir/.env" ]; then
  set -a
  # shellcheck disable=SC1091
  . "$repo_dir/.env"
  set +a
fi

pio_bin=${PLATFORMIO_CLI_BIN:-platformio}
# Production env by default; COLIBRINO_PIO_ENV=m5stack-sticks3-noble selects the
# instrumentation-only variant (same guards, same runbook).
pio_env=${COLIBRINO_PIO_ENV:-m5stack-sticks3}
ota_host=${COLIBRINO_OTA_HOST:-sticks3-ptt.local}
secrets_file=${COLIBRINO_OTA_SECRETS:-}
expected_mac=${COLIBRINO_OTA_EXPECTED_MAC:-}

# The env name becomes a path under .pio/build that this script deletes link
# products from, so it must be a plain PlatformIO env name.
case "$pio_env" in
  ''|*[!A-Za-z0-9_-]*)
    echo "COLIBRINO_PIO_ENV must be a plain PlatformIO env name" >&2
    exit 1
    ;;
esac

if [ -z "$secrets_file" ] || [ ! -f "$secrets_file" ]; then
  echo "COLIBRINO_OTA_SECRETS must name the ignored StickS3 secrets header" >&2
  exit 1
fi

ota_password=$(awk '/^[[:space:]]*#define[[:space:]]+OTA_PASS[[:space:]]+/ {gsub(/\"/, "", $3); print $3; exit}' "$secrets_file")
if [ -z "$ota_password" ]; then
  echo "OTA_PASS is missing from COLIBRINO_OTA_SECRETS" >&2
  exit 1
fi

if [ -z "$expected_mac" ]; then
  echo "COLIBRINO_OTA_EXPECTED_MAC is required to protect other S3 devices" >&2
  exit 1
fi
expected_mac=$(printf '%s' "$expected_mac" | tr '[:upper:]' '[:lower:]')

# Hostname + ARP-MAC identity guard. Run before the build (fail early) and
# again right before the upload, because the build takes seconds to minutes
# and the upload must target the hardware verified a moment ago, not an
# address the network may have handed to another S3 meanwhile.
verify_target() {
  ota_ip=$(ping -c 1 -W 2000 "$ota_host" 2>/dev/null | awk -F'[()]' '/PING/ {print $2; exit}')
  if [ -z "$ota_ip" ]; then
    echo "$ota_host did not resolve; wake the Colibrino StickS3 and retry" >&2
    exit 1
  fi

  actual_mac=$(arp -n "$ota_ip" 2>/dev/null | awk '/ at / {print $4; exit}')
  actual_mac=$(printf '%s' "$actual_mac" | tr '[:upper:]' '[:lower:]')
  if [ -z "$actual_mac" ] || [ "$actual_mac" != "$expected_mac" ]; then
    echo "Refusing OTA: $ota_host resolved to unexpected hardware $actual_mac" >&2
    exit 1
  fi
}

verify_target

cd "$project_dir"
build_dir="$project_dir/.pio/build/$pio_env"
firmware="$build_dir/firmware.bin"
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
build_log="$tmp_dir/build.log"
build_status="$tmp_dir/status"
build_started="$tmp_dir/started"
: > "$build_started"

# The post-link gate (scripts/check_image.py) is a SCons post-action on the
# .elf: it runs only when the image is actually relinked. Remove the previous
# link products so every OTA build relinks, the gate runs on exactly the image
# that will be uploaded, and a stale firmware.bin can never be picked up.
rm -f "$build_dir/firmware.elf" "$firmware"

# The OTA lane always runs the image gate at its production ceiling: a shell
# export or .env line must not rebase it for an upload.
unset COLIBRINO_DRAM_CEILING

# Under plain /bin/sh a pipeline reports tee's status, not the build's, so the
# build status is captured explicitly (in a subshell with errexit off, or the
# status line would never be written); a failed build (compile, link, or a
# CHECK_IMAGE,FAIL from the gate) refuses the upload instead of shipping
# whatever firmware.bin is on disk.
( set +e; "$pio_bin" run -e "$pio_env" 2>&1; echo "$?" > "$build_status" ) | tee "$build_log"
if [ "$(cat "$build_status")" != "0" ]; then
  echo "Refusing OTA: the build failed (see the output above)" >&2
  exit 1
fi

build_id=$(sed -n 's/^COLIBRINO_BUILD_ID=//p' "$build_log" | tail -1)
if [ -z "$build_id" ]; then
  echo "Could not read COLIBRINO_BUILD_ID from the build output" >&2
  exit 1
fi

if ! grep -q '^CHECK_IMAGE,PASS' "$build_log"; then
  echo "Refusing OTA: the post-link image gate did not report CHECK_IMAGE,PASS" >&2
  exit 1
fi

if [ ! -f "$firmware" ] || [ -z "$(find "$firmware" -newer "$build_started" 2>/dev/null)" ]; then
  echo "Refusing OTA: $firmware is missing or older than this build" >&2
  exit 1
fi

core_json=$("$pio_bin" system info --json-output)
core_dir=$(printf '%s\n' "$core_json" | sed -E 's/.*"core_dir": \{"title": "[^"]+", "value": "([^"]+)".*/\1/')
espota="$core_dir/packages/framework-arduinoespressif32/tools/espota.py"
pio_python="$core_dir/penv/bin/python"

if [ ! -f "$espota" ] || [ ! -x "$pio_python" ]; then
  echo "PlatformIO OTA tools are missing" >&2
  exit 1
fi

verify_target

echo "OTA env=$pio_env build=$build_id -> $ota_host ($ota_ip); bedside-countdown-s3 is never a target"
"$pio_python" "$espota" -i "$ota_ip" -p 3232 --auth="$ota_password" -f "$firmware"
