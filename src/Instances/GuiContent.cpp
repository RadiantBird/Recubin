#include <Instances/GuiContent.hpp>
#include <include/Core/Renderer.hpp>

void ImageContent::setImage(const std::string& p) {
    path = p;
    if (!p.empty() && Renderer::instance)
        textureID = Renderer::instance->loadTexture(p.c_str());
}
