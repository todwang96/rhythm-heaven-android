#!/bin/bash
set -euo pipefail
if [ "$#" -ne 2 ]; then
    echo "Usage: tools/iphone_probe.sh DEVICE_ID DEVELOPMENT_TEAM" >&2
    exit 2
fi
PROBE_DEVICE=$1
PROBE_TEAM=$2
PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_ROOT"
PROBE_BUILD="$PROJECT_ROOT/../.nitenphone/build/probes"
mkdir -p "$PROBE_BUILD/logs"
xcodebuild -project platform/ios/probe/Probe.xcodeproj -scheme NitenphoneProbe \
    -configuration Debug -sdk iphoneos -derivedDataPath "$PROBE_BUILD/ios" \
    -destination "id=$PROBE_DEVICE" -allowProvisioningUpdates \
    "DEVELOPMENT_TEAM=$PROBE_TEAM" INFOPLIST_FILE=Probe/Info.plist \
    ALWAYS_SEARCH_USER_PATHS=NO build > "$PROBE_BUILD/logs"/ios-probe-build.log 2>&1
xcrun devicectl device install app --device "$PROBE_DEVICE" \
    "$PROBE_BUILD/ios"/Build/Products/Debug-iphoneos/NitenphoneProbe.app \
    > "$PROBE_BUILD/logs"/ios-probe-install.log 2>&1
echo "Installed Nitenphone CPU Test. Unlock the phone before launch."
xcrun devicectl device process launch --device "$PROBE_DEVICE" \
    --terminate-existing com.mmmz.NitenphoneCPUProbe
echo "The app saves Documents/cpu-probe.json in its own container."
echo "This app tests the CPU bridge. It does not contain game files or keys."
