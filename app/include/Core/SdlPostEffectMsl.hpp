#pragma once

#include <cstdint>
#include <span>
#include <string>

// Converts a fragment SPIR-V module to Metal Shading Language for SDL_GPU.
// The returned entry point is the name emitted by SPIRV-Cross.
bool compileSdlPostEffectMsl(std::span<const std::uint32_t> spirv,
                             std::string& source,
                             std::string& entryPoint,
                             std::string& error);
