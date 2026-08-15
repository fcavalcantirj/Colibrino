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
ota_host=${COLIBRINO_OTA_HOST:-sticks3-ptt.local}
secrets_file=${COLIBRINO_OTA_SECRETS:-}
expected_mac=${COLIBRINO_OTA_EXPECTED_MAC:-}

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

ota_ip=$(ping -c 1 -W 2000 "$ota_host" 2>/dev/null | awk -F'[()]' '/PING/ {print $2; exit}')
if [ -z "$ota_ip" ]; then
  echo "$ota_host did not resolve; wake the Colibrino StickS3 and retry" >&2
  exit 1
fi

actual_mac=$(arp -n "$ota_ip" 2>/dev/null | awk '/ at / {print $4; exit}')
actual_mac=$(printf '%s' "$actual_mac" | tr '[:upper:]' '[:lower:]')
expected_mac=$(printf '%s' "$expected_mac" | tr '[:upper:]' '[:lower:]')
if [ -z "$actual_mac" ] || [ "$actual_mac" != "$expected_mac" ]; then
  echo "Refusing OTA: $ota_host resolved to unexpected hardware $actual_mac" >&2
  exit 1
fi

cd "$project_dir"
"$pio_bin" run -e m5stack-sticks3

core_json=$("$pio_bin" system info --json-output)
core_dir=$(printf '%s\n' "$core_json" | sed -E 's/.*"core_dir": \{"title": "[^"]+", "value": "([^"]+)".*/\1/')
espota="$core_dir/packages/framework-arduinoespressif32/tools/espota.py"
pio_python="$core_dir/penv/bin/python"
firmware="$project_dir/.pio/build/m5stack-sticks3/firmware.bin"

if [ ! -f "$espota" ] || [ ! -x "$pio_python" ] || [ ! -f "$firmware" ]; then
  echo "PlatformIO OTA tools or built firmware are missing" >&2
  exit 1
fi

echo "OTA -> $ota_host ($ota_ip); bedside-countdown-s3 is never a target"
"$pio_python" "$espota" -i "$ota_ip" -p 3232 --auth="$ota_password" -f "$firmware"
