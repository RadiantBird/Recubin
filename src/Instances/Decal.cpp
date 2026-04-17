#include <include/Instances/Decal.hpp>

static const char* faceNames[] = { "Front", "Back", "Top", "Bottom", "Right", "Left" };

Decal::Decal(unsigned int textureID, Face targetFace) 
    : Instance("Decal_" + std::string(faceNames[(int)targetFace])), TextureID(textureID), face(targetFace) {}

Decal::~Decal() {}

std::string Decal::GetClassName() {
    return "Decal";
}

bool Decal::IsA(std::string className) {
    if (className == "Decal") return true;
    return Instance::IsA(className);
}
