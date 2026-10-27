#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${CG_BUILD_DIR:-${project_dir}/build-debug}"
if [[ "$(uname -s)" == Darwin && -d /opt/homebrew ]]; then
    export DYLD_LIBRARY_PATH="/opt/homebrew/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
    export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json}"
    export VK_LAYER_PATH="${VK_LAYER_PATH:-/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d}"
fi
if [[ ! -x "${build_dir}/vulkan-starter-app" ]]; then
    echo "Build first: cmake --preset debug && cmake --build build-debug --parallel" >&2
    exit 1
fi
exec "${build_dir}/vulkan-starter-app" "$@"
