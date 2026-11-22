#pragma once

#include <Core/SdlGpuWindow.hpp>

#include <optional>
#include <string>

int runSdlScenePreview(const std::string& scenePath,
                       const std::string& title,
                       SdlGpuDriver driver,
                       const std::optional<std::string>& captureDir,
                       bool studio = false);
