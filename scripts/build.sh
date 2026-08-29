#!/usr/bin/env bash
# Build, then verify the plugin actually landed in ~/Library/Audio/Plug-Ins.
#
# Live holds plugin bundles open while it runs, so the copy-after-build step can
# fail silently and leave you auditioning a stale binary. This checks.
#
#   ./scripts/build.sh            build + verify
#   ./scripts/build.sh --run      also launch the standalone
#   ./scripts/build.sh --test     also run ctest

set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=build/Vellum_artefacts/RelWithDebInfo
INSTALL="$HOME/Library/Audio/Plug-Ins"

if pgrep -qf "Ableton Live.*MacOS/Live"; then
    echo "note: Ableton Live is running. It holds plugin bundles open, so the"
    echo "      install step may fail, and Live keeps the previously loaded"
    echo "      binary mapped until you quit and reopen it."
    echo
fi

cmake --build build --parallel

stale=0
for pair in "VST3/Vellum.vst3:VST3/Vellum.vst3" "AU/Vellum.component:Components/Vellum.component"; do
    built="$BUILD/${pair%%:*}/Contents/MacOS/Vellum"
    installed="$INSTALL/${pair##*:}/Contents/MacOS/Vellum"

    if [[ ! -f $installed ]]; then
        echo "MISSING: $installed"; stale=1
    elif ! cmp -s "$built" "$installed"; then
        echo "STALE:   $installed"; stale=1
    else
        echo "current: ${pair##*:}"
    fi
done

if (( stale )); then
    echo
    echo "Quit Ableton Live and rebuild, or copy manually:"
    echo "  cmake --build build --target Vellum_VST3 Vellum_AU"
    exit 1
fi

[[ ${1:-} == --test ]] && ctest --test-dir build --output-on-failure
[[ ${1:-} == --run  ]] && open "$BUILD/Standalone/Vellum.app"

exit 0
