#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

SKETCH="dakiller"

# --- Localizar arduino-cli ---
ARDUINO_CLI="${ARDUINO_CLI:-}"
if [ -z "$ARDUINO_CLI" ]; then
  if command -v arduino-cli >/dev/null 2>&1; then
    ARDUINO_CLI="$(command -v arduino-cli)"
  elif [ -x "$HOME/.local/bin/arduino-cli" ]; then
    ARDUINO_CLI="$HOME/.local/bin/arduino-cli"
  else
    echo "Error: no se encontró arduino-cli." >&2
    exit 1
  fi
fi

# --- Localizar esptool ---
find_esptool() {
  if [ -n "${ESPTOOL:-}" ] && [ -f "${ESPTOOL}" ]; then
    echo "$ESPTOOL"
    return 0
  fi
  if command -v esptool >/dev/null 2>&1; then
    command -v esptool
    return 0
  fi

  local candidate
  candidate="$(find "$HOME/.arduino15/packages/esp32/tools/esptool_py" -type f -name esptool 2>/dev/null | head -n1 || true)"
  if [ -n "$candidate" ]; then
    echo "$candidate"
    return 0
  fi

  candidate="$(find "$HOME/.arduino15/packages" -type f \( -name esptool -o -name esptool.py \) 2>/dev/null | head -n1 || true)"
  if [ -n "$candidate" ]; then
    echo "$candidate"
    return 0
  fi

  return 1
}

ESPTOOL_BIN="$(find_esptool || true)"
if [ -z "$ESPTOOL_BIN" ]; then
  echo "Error: no se encontró esptool. Instala los cores de ESP8266 y/o ESP32." >&2
  exit 1
fi

run_esptool() {
  if [ -x "$ESPTOOL_BIN" ]; then
    "$ESPTOOL_BIN" "$@"
  else
    python3 "$ESPTOOL_BIN" "$@"
  fi
}

# --- Puerto serie ---
PORT="${1:-}"
FQBN_OVERRIDE="${2:-${FQBN:-}}"

if [ -z "$PORT" ]; then
  mapfile -t PORTS < <(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
  if [ "${#PORTS[@]}" -eq 0 ]; then
    echo "Error: no hay puertos serie (/dev/ttyUSB* o /dev/ttyACM*)." >&2
    exit 1
  elif [ "${#PORTS[@]}" -gt 1 ]; then
    echo "Hay varios puertos serie. Indica cuál usar:" >&2
    printf '  %s\n' "${PORTS[@]}" >&2
    echo "Uso: $0 <puerto> [fqbn]" >&2
    exit 1
  fi
  PORT="${PORTS[0]}"
fi

if [ ! -e "$PORT" ]; then
  echo "Error: el puerto $PORT no existe." >&2
  exit 1
fi

echo "Puerto: $PORT"
echo "Detectando chip..."
CHIP_OUTPUT="$(run_esptool --port "$PORT" chip-id 2>&1 || true)"
if ! echo "$CHIP_OUTPUT" | grep -qi "Connected to ESP"; then
  # Compatibilidad con versiones antiguas de esptool
  CHIP_OUTPUT="$(run_esptool --port "$PORT" chip_id 2>&1 || true)"
fi
echo "$CHIP_OUTPUT" | sed -E 's/\x1B\[[0-9;]*[A-Za-z]//g' | sed -n '1,25p'

if echo "$CHIP_OUTPUT" | grep -qi "ESP8266"; then
  DEFAULT_FQBN="esp8266:esp8266:nodemcuv2"
  UPLOAD_FQBN="${DEFAULT_FQBN}:baud=57600"
elif echo "$CHIP_OUTPUT" | grep -qi "ESP32-S3"; then
  DEFAULT_FQBN="esp32:esp32:esp32s3"
  UPLOAD_FQBN="$DEFAULT_FQBN"
elif echo "$CHIP_OUTPUT" | grep -qi "ESP32-C3"; then
  DEFAULT_FQBN="esp32:esp32:esp32c3"
  UPLOAD_FQBN="$DEFAULT_FQBN"
elif echo "$CHIP_OUTPUT" | grep -qi "ESP32-C6"; then
  DEFAULT_FQBN="esp32:esp32:esp32c6"
  UPLOAD_FQBN="$DEFAULT_FQBN"
elif echo "$CHIP_OUTPUT" | grep -qi "ESP32"; then
  DEFAULT_FQBN="esp32:esp32:esp32"
  UPLOAD_FQBN="$DEFAULT_FQBN"
else
  echo "Error: no se pudo identificar el chip. Comprueba la conexión y el puerto." >&2
  exit 1
fi

if [ -n "$FQBN_OVERRIDE" ]; then
  DEFAULT_FQBN="$FQBN_OVERRIDE"
  UPLOAD_FQBN="$FQBN_OVERRIDE"
fi

echo "FQBN de compilación: $DEFAULT_FQBN"
echo "FQBN de subida:       $UPLOAD_FQBN"
echo

echo "Compilando $SKETCH..."
"$ARDUINO_CLI" compile --fqbn "$DEFAULT_FQBN" "$SKETCH"

echo
echo "Subiendo a $PORT..."
"$ARDUINO_CLI" upload -p "$PORT" --fqbn "$UPLOAD_FQBN" "$SKETCH"

echo
echo "Listo. Firmware subido a $PORT."
