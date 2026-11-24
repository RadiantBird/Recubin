#pragma once

#include <SDL3/SDL_gpu.h>

#include <array>
#include <unordered_map>

class BaseCube;
using SdlSurfaceGuiLayers = std::unordered_map<const BaseCube*, std::array<SDL_GPUTexture*, 6>>;
