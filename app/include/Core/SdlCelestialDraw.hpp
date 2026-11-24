#pragma once

#include <array>
#include <type_traits>

// Parameters for one SDL GPU celestial billboard. The layout is shared by the
// generated vertex and fragment shader uniform blocks.
struct alignas(16) SdlCelestialDraw {
    std::array<float, 4> placement{};
    std::array<float, 4> occlusion{};
    std::array<float, 4> color{};
    std::array<float, 4> directionAndMode{};
    std::array<float, 4> projection{};
    std::array<float, 4> glow{};
    std::array<float, 4> spikes{};
    std::array<float, 4> appearance{};
    std::array<float, 4> veil{};
};

static_assert(std::is_standard_layout_v<SdlCelestialDraw>);
static_assert(sizeof(SdlCelestialDraw) == 9 * sizeof(std::array<float, 4>));
