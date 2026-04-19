#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vksumi
{
    // push-constant block, matches color.frag's Knobs layout exactly.
    // 0 = no change for every knob. positive = more / brighter.
    struct alignas(16) Knobs
    {
        float brightness = 0.0f;   // [-1 .. 1]
        float contrast   = 0.0f;   // [-1 .. 1]
        float exposure   = 0.0f;   // EV stops, mul = 2^value, [-3 .. 3]
        float saturation = 0.0f;   // -1 = grayscale, [-1 .. 1]

        float vibrance   = 0.0f;   // smarter saturation
        float hue_deg    = 0.0f;   // [-180 .. 180]
        float gamma      = 0.0f;   // + brightens midtones
        float temperature = 0.0f;  // warm(+) cool(-)

        float tint       = 0.0f;   // magenta(+) green(-)
        float red_gain   = 0.0f;
        float green_gain = 0.0f;
        float blue_gain  = 0.0f;

        float shadows    = 0.0f;
        float midtones   = 0.0f;
        float highlights = 0.0f;
        float _pad       = 0.0f;
    };

    static_assert(sizeof(Knobs) == 64, "Knobs layout drift");

    struct Config
    {
        Knobs       knobs;
        bool        enabled     = true;
        std::string toggle_keys = "Shift_R+F9";
        std::vector<std::string> sources;
    };

    std::shared_ptr<const Config> loadConfig();
    std::shared_ptr<const Config> currentConfig();
    void                          setCurrentConfig(std::shared_ptr<const Config> next);

    void startConfigWatcher();
    void stopConfigWatcher();

    // drop a per-game .conf in ~/.config/vkSumi/games/ if missing, then reload.
    // called from the swapchain hook so Wine helpers like explorer.exe (which
    // never make a swapchain) dont end up polluting the games dir
    void ensurePerGameConfig();
}
