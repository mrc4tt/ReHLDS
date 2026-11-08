#!/bin/bash
# Wrapper around `docker compose run` for the local ReHLDS build.
# Exports the host UID/GID so the container writes build/ as you, not root.
#
#   ./docker-build.sh             # unittest + release build
#   ./docker-build.sh unittest    # unittest build only
#   ./docker-build.sh release     # release build only
set -euo pipefail
cd "$(dirname "$0")"
# UID is a readonly bash builtin, so pass the IDs under our own names.
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
exec docker compose run --rm build "$@"
