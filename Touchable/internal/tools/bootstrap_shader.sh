#!/bin/bash
set -euo pipefail
PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_ROOT"
LOCAL_ROOT="$PROJECT_ROOT/../.nitenphone"
DEPENDENCIES="$LOCAL_ROOT/dependencies"
mkdir -p "$DEPENDENCIES" "$LOCAL_ROOT/build/tooling/downloads" "$LOCAL_ROOT/build/tooling/logs"
DOTNET_VERSION=10.0.401
DOTNET_HASH=69f64eb00dc045398755c440b152225d544301a345a146a16e86a56a0c52b7c94b2c331520e976dbb821f18d31930aafbd25bb85961e3517e0665414ce0cbcff
DOTNET_ARCHIVE="$LOCAL_ROOT/build/tooling/downloads/dotnet-sdk-10.0.401-osx-arm64.tar.gz"
if [[ ! -x "${DEPENDENCIES}/dotnet/dotnet" ]]; then
    curl --fail --location --retry 3 --silent --show-error \
        "https://builds.dotnet.microsoft.com/dotnet/Sdk/$DOTNET_VERSION/dotnet-sdk-$DOTNET_VERSION-osx-arm64.tar.gz" -o "$DOTNET_ARCHIVE"
    ACTUAL_HASH=$(shasum -a 512 "$DOTNET_ARCHIVE" | awk '{print $1}')
    [[ "$ACTUAL_HASH" == "$DOTNET_HASH" ]] || { echo 'Dotnet checksum mismatch' >&2; exit 1; }
    mkdir -p "${DEPENDENCIES}/dotnet"
    tar -xzf "$DOTNET_ARCHIVE" -C "${DEPENDENCIES}/dotnet"
fi
[[ $("${DEPENDENCIES}/dotnet/dotnet" --version) == "$DOTNET_VERSION" ]] || { echo 'Dotnet version mismatch' >&2; exit 1; }
SPIRV_COMMIT=d8e3e2b141b8c8a167b2e3984736a6baacff316c
if [[ ! -d "${DEPENDENCIES}/SPIRV-Cross/.git" ]]; then
    git clone --depth 1 --branch vulkan-sdk-1.4.321.0 https://github.com/KhronosGroup/SPIRV-Cross.git "${DEPENDENCIES}/SPIRV-Cross" > "${LOCAL_ROOT}/build/tooling/logs/spirv-fetch.log" 2>&1
fi
[[ $(git -C "${DEPENDENCIES}/SPIRV-Cross" rev-parse HEAD) == "$SPIRV_COMMIT" ]] || { echo 'SPIRV-Cross revision mismatch' >&2; exit 1; }
make -C "${DEPENDENCIES}/SPIRV-Cross" -j 6 > "${LOCAL_ROOT}/build/tooling/logs/spirv-build.log" 2>&1
DOTNET_CLI_TELEMETRY_OPTOUT=1 "${DEPENDENCIES}/dotnet/dotnet" build \
    "${DEPENDENCIES}/d2r-mobile/dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj" -c Release \
    > "${LOCAL_ROOT}/build/tooling/logs/translator-build.log" 2>&1
echo 'Shader translation tools built.'
