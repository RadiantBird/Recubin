#include "Studio/Studio.hpp"

#include <Util/RuntimeLaunchArgs.hpp>
#include <iostream>

#ifdef RECUBIN_SDL_GPU_STUDIO
#include <Core/SdlScenePreview.hpp>
#include <Util/IPlatform.hpp>
#include <Util/Platform.hpp>
#endif

int main(int argc, char* argv[])
{
    const auto runtime = parseRuntimeLaunchArgs(argc, argv);
    if (!runtime.valid) {
        std::cerr << runtime.error << '\n';
        return 1;
    }
#ifndef RECUBIN_SDL_GPU_STUDIO
    if (runtime.gpuDriver && !runtime.gpuPreview) {
        std::cerr << "--gpu-driver requires an xmake build with --sdl-gpu-preview=y\n";
        return 1;
    }
#endif
    if (runtime.gpuPreview) {
        if (!runtime.scenePath) {
            std::cerr << "Studio SDL GPU preview requires --gpu-preview and --scene <path>\n";
            return 1;
        }
#ifdef RECUBIN_SDL_GPU_STUDIO
        getPlatform().setupConsoleUtf8();
        return runSdlScenePreview(*runtime.scenePath,
                                  runtime.windowTitle.value_or("Recubin Studio SDL GPU"),
                                  runtime.gpuDriver.value_or(SdlGpuDriver::Auto), runtime.selfCheckDir, true);
#else
        std::cerr << "--gpu-preview requires an xmake build with --sdl-gpu-preview=y\n";
        return 1;
#endif
    }
    const auto launch = StudioMain::prepareLaunch(argc, argv);
    if (const auto* exitCode = std::get_if<int>(&launch)) {
        return *exitCode;
    }
    return StudioMain::runStudio(std::get<StudioMain::LaunchOptions>(launch));
}
