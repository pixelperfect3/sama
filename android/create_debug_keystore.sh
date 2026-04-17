#!/bin/bash
# ---------------------------------------------------------------------------
# Create a debug keystore for signing Android APKs during development.
#
# Usage: ./android/create_debug_keystore.sh [keystore_path]
#
# Default path: $HOME/.android/debug.keystore
# ---------------------------------------------------------------------------

set -euo pipefail

DEBUG_KS="${1:-$HOME/.android/debug.keystore}"

if [ -f "$DEBUG_KS" ]; then
    echo "Debug keystore already exists: $DEBUG_KS"
    exit 0
fi

mkdir -p "$(dirname "$DEBUG_KS")"

keytool -genkey -v -keystore "$DEBUG_KS" \
    -storepass android -alias androiddebugkey -keypass android \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Debug,O=Sama,L=Debug,C=US"

echo "Created debug keystore: $DEBUG_KS"
