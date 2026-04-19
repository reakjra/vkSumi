#!/usr/bin/env fish
# Run a Vulkan app with the local vksumi build force-loaded.
# Usage: ./scripts/run.fish vkcube
#        ./scripts/run.fish %command% (Steam launch options)

set -l root (realpath (dirname (status filename))/..)
set -x VK_LAYER_PATH        "$root/layer"
set -x LD_LIBRARY_PATH      "$root/builddir/src" $LD_LIBRARY_PATH
set -x VK_INSTANCE_LAYERS   VK_LAYER_vksumi_color_grading
set -x VKSUMI_DEBUG         1

if test (count $argv) -eq 0
    echo "usage: scripts/run.fish <vulkan-app> [args...]" >&2
    exit 2
end

exec $argv
