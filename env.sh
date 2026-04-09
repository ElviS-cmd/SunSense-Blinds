#!/usr/bin/env bash

# Resolve the project root relative to this script's location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SUNSENSE_PROJECT_ROOT="$SCRIPT_DIR"

# IDF_PATH must point to your ESP-IDF installation.
# Set it before sourcing this script, or export it in your shell profile.
# Example: export IDF_PATH="$HOME/esp/esp-idf-5.4.3"
if [ -z "$IDF_PATH" ]; then
    echo "IDF_PATH is not set. Set it to your ESP-IDF installation directory." >&2
    return 1 2>/dev/null || exit 1
fi

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ESP-IDF export script not found at $IDF_PATH/export.sh" >&2
    echo "Set IDF_PATH to your ESP-IDF installation directory." >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1091
source "$IDF_PATH/export.sh"

cd "$SUNSENSE_PROJECT_ROOT" || return 1 2>/dev/null || exit 1
