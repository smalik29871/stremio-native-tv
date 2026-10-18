# Shared settings for the build scripts. Override with environment variables.
# Toolchain tarballs come from https://github.com/openlgtv/buildroot-nc4/releases (Linux)
# and https://github.com/webosbrew/native-toolchain/releases (macOS).
: "${WEBOS_TOOLCHAIN:=$HOME/arm-webos-linux-gnueabi_sdk-buildroot}"
: "${WEBOS_TOOLCHAIN_FILE:=$WEBOS_TOOLCHAIN/share/buildroot/toolchainfile.cmake}"
: "${TV_IP:=}"                       # LG TV with Developer Mode / Homebrew Channel root
: "${TIZEN_STUDIO:=$HOME/tizen-studio}"
: "${TIZEN_PROFILE:=tv-samsung-5.5}" # for the WebAssembly .wgt
: "${TIZEN_NATIVE_PROFILE:=tv-5.0}"  # Tizen native rootstrap used for the .tpk
: "${EMSDK:=$HOME/emsdk}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
