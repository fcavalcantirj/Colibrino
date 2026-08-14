#!/bin/sh

# Reproducible software-only gate. It never uploads to a physical device.
set -eu

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_directory=$(CDPATH= cd -- "$script_directory/.." && pwd)
repository_directory=$(CDPATH= cd -- "$project_directory/.." && pwd)

if [ -f "$repository_directory/.env" ]; then
  # Export dotenv assignments for Wokwi without printing their values.
  set -a
  . "$repository_directory/.env"
  set +a
fi

if [ -z "${WOKWI_CLI_TOKEN:-}" ]; then
  echo "WOKWI_CLI_TOKEN is required (export it or add it to the ignored repository .env)." >&2
  exit 2
fi

platformio_cli=${PLATFORMIO_CLI_BIN:-}
if [ -z "$platformio_cli" ]; then
  if command -v platformio >/dev/null 2>&1; then
    platformio_cli=$(command -v platformio)
  elif command -v pio >/dev/null 2>&1; then
    platformio_cli=$(command -v pio)
  else
    echo "PlatformIO CLI was not found. Set PLATFORMIO_CLI_BIN to its executable." >&2
    exit 2
  fi
fi

wokwi_cli=${WOKWI_CLI_BIN:-}
if [ -z "$wokwi_cli" ]; then
  if command -v wokwi-cli >/dev/null 2>&1; then
    wokwi_cli=$(command -v wokwi-cli)
  else
    echo "Wokwi CLI was not found. Set WOKWI_CLI_BIN to its executable." >&2
    exit 2
  fi
fi

cd "$project_directory"
# Build first so wokwi.toml always points to a current ELF and firmware image.
"$platformio_cli" run -e wokwi-esp32s3
# Lint the virtual wiring independently from firmware execution.
"$wokwi_cli" lint . --warnings-as-errors
# A timeout or missing PASS marker is a failure; an explicit FAIL exits sooner.
"$wokwi_cli" . \
  --timeout 15000 \
  --expect-text COLIBRINO_SIM_PASS \
  --fail-text COLIBRINO_SIM_FAIL
