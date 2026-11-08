#!/bin/bash
# Runs INSIDE the debian:11-slim container (see docker/Dockerfile).
# Mirrors the build, unittest and packaging steps of the linux job in build.yml.
#
#   build.sh            -> unittest build + run, then release build + package
#   build.sh unittest   -> unittest build + run only
#   build.sh release    -> release build + package only
#
# Output of the release/package step lands in publish/ — the same layout the
# CI workflow uploads as the linux32-rehlds artifact.
set -euo pipefail

MODE="${1:-all}"

# The repo is bind-mounted from the host, so its files are owned by the
# host user, not by whoever runs inside the container. appversion.sh shells
# out to git, which refuses "dubious ownership" dirs without this.
git config --global --add safe.directory /src

run_unittest_build() {
    echo "==> Unittests build"
    rm -rf build
    cmake -DCMAKE_BUILD_TYPE=Unittests -B build
    cmake --build build -j"$(nproc)"

    echo "==> Running unittests"
    local retVal=0
    LD_LIBRARY_PATH="rehlds/lib/linux32:${LD_LIBRARY_PATH:-}" \
        ./build/rehlds/engine_i486 2>/dev/null || retVal=$?
    # CI contract: exit code 0 or 3 = pass; anything else = failure.
    if [ "$retVal" -ne 0 ] && [ "$retVal" -ne 3 ]; then
        echo "Unittests FAILED (exit $retVal)"
        exit 1
    fi
    echo "Unittests OK (exit $retVal)"
}

run_release_build() {
    echo "==> Release build (GCC)"
    rm -rf build
    cmake -B build
    cmake --build build -j"$(nproc)"
}

# Assemble publish/ exactly like the CI "Prepare HLSDK", "Move files" and
# "Compute and bundle engine CRC" steps. Uses cp (not the workflow's mv) so
# the bind-mounted source tree and build/ outputs survive for re-runs --
# mv here would delete the vendored libsteam_api.so from rehlds/lib/linux32.
package_publish() {
    echo "==> Packaging publish/"
    rm -rf publish

    # HLSDK headers
    mkdir -p publish/hlsdk
    rsync -a rehlds/common/        publish/hlsdk/common/
    rsync -a rehlds/dlls/          publish/hlsdk/dlls/
    rsync -a rehlds/pm_shared/     publish/hlsdk/pm_shared/
    rsync -a rehlds/public/        publish/hlsdk/public/ --exclude rehlds/
    rsync -a rehlds/public/rehlds/ publish/hlsdk/engine

    # Binaries
    mkdir -p publish/bin/linux32/valve/dlls publish/bin/linux32/cstrike/dlls
    cp rehlds/lib/linux32/libsteam_api.so                           publish/bin/linux32/libsteam_api.so
    cp build/rehlds/engine_i486.so                                  publish/bin/linux32/engine_i486.so
    cp rehlds/version/appversion.h                                  publish/appversion.h
    cp build/rehlds/dedicated/hlds_linux                            publish/bin/linux32/hlds_linux
    cp build/rehlds/HLTV/Console/hltv                               publish/bin/linux32/hltv
    cp build/rehlds/HLTV/Core/core.so                               publish/bin/linux32/core.so
    cp build/rehlds/HLTV/Proxy/proxy.so                             publish/bin/linux32/proxy.so
    cp build/rehlds/HLTV/DemoPlayer/demoplayer.so                   publish/bin/linux32/demoplayer.so
    cp build/rehlds/HLTV/Director/director.so                       publish/bin/linux32/valve/dlls/director.so
    cp build/rehlds/HLTV/Director/director.so                       publish/bin/linux32/cstrike/dlls/director.so
    cp build/rehlds/filesystem/FileSystem_Stdio/filesystem_stdio.so publish/bin/linux32/filesystem_stdio.so

    # Bundle the engine CRC next to the binary (CI "Compute and bundle" step).
    local crc
    crc=$(cd publish/bin/linux32 && crc32 engine_i486.so | tr '[:lower:]' '[:upper:]')
    echo "$crc  engine_i486.so" > publish/bin/linux32/engine_i486.so.crc
    echo "Engine CRC32: $crc"

    # GLIBC/ABI compat check on every shipped binary (CI compat test step).
    echo "==> GLIBC/ABI compat test"
    bash ./rehlds/version/glibc_test.sh \
        publish/bin/linux32/engine_i486.so \
        publish/bin/linux32/hlds_linux \
        publish/bin/linux32/hltv \
        publish/bin/linux32/core.so \
        publish/bin/linux32/proxy.so \
        publish/bin/linux32/demoplayer.so \
        publish/bin/linux32/valve/dlls/director.so \
        publish/bin/linux32/filesystem_stdio.so

    echo "==> publish/ ready:"
    find publish -type f | sort | sed 's/^/    /'
}

case "$MODE" in
    unittest) run_unittest_build ;;
    release)  run_release_build; package_publish ;;
    all)      run_unittest_build; run_release_build; package_publish ;;
    *) echo "Usage: build.sh [all|unittest|release]"; exit 2 ;;
esac

echo "==> Done."
