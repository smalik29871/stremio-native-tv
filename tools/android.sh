#!/usr/bin/env bash
# Builds the Android TV / Fire TV APK (docs/ANDROID_TV.md).
#   ANDROID_HOME=<sdk> tools/android.sh           -> deploy/android/app/build/outputs/apk/release/
# Needs the Android SDK (platform 35, build-tools, NDK 27.2.12479018, CMake 3.22.1), a JDK 17+
# and Gradle 8.9+ ("gradle" on PATH, or GRADLE=<path>). The service keys are read from the
# environment like the webOS build (TMDB_API_KEY, ...); signing from ANDROID_KEYSTORE and
# ANDROID_KEY_PASSWORD, else the build is debug-signed (fine for testing).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${ANDROID_HOME:?set ANDROID_HOME to the Android SDK}"
bash "$ROOT/tools/android-deps.sh"
cd "$ROOT/deploy/android"
echo "sdk.dir=$ANDROID_HOME" > local.properties
"${GRADLE:-gradle}" --no-daemon -q assembleRelease
ls -l app/build/outputs/apk/release/*.apk
