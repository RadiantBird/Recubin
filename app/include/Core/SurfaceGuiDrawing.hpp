#pragma once

#include <include/imgui/imgui.h>

class SurfaceGui;

struct SurfaceGuiLayout {
    float cW = 0.0f;
    float cH = 0.0f;
    int w = 0;
    int h = 0;
    float scale = 1.0f;
    float offX = 0.0f;
    float offY = 0.0f;
};

bool computeSurfaceGuiLayout(const SurfaceGui& surface, SurfaceGuiLayout& result);
void collectImGuiAtlasTextures(ImVector<ImTextureData*>& textures);
