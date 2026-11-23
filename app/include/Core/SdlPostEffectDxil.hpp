#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Converts a fragment SPIR-V module to DXIL for SDL_GPU's D3D12 backend.
// The returned byte vector contains the DXIL object accepted by SDL_CreateGPUShader.
bool compileSdlPostEffectDxil(std::span<const std::uint32_t> spirv,
                              std::vector<std::uint8_t>& dxil,
                              std::string& error);
