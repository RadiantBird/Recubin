#include <Instances/LiquidCube.hpp>
#include <Instances/Cube.hpp>          // s_VAO / defaultTextureID 流用
#include <include/Core/PropertyRegistry.hpp>
#include <Util/GLUniformCache.hpp>
#include <GL/glew.h>
#include <cmath>

static const bool s_liquidCubeRegistered = []{
    using namespace PropertyRegistry;
    registerClass("LiquidCube", {
        field<&LiquidCube::Density>("Density", 0.0f, 50.0f, 0.1f),
    });
    return true;
}();

float LiquidCube::waveHeight(
    float normalizedX, float normalizedZ, float time) {
    return std::sin(time * WAVE_ANGULAR_SPEED +
                    normalizedX * WAVE_SPATIAL_FREQUENCY +
                    normalizedZ * WAVE_SPATIAL_FREQUENCY) * WAVE_AMPLITUDE;
}

LiquidCube::LiquidCube(Vector3 Pos, Vector3 Sz)
    : Named<LiquidCube, BaseCube>(Pos, Sz)
{
    CanCollide = false;                       // 物理コリジョン無し（actor 不要）
    CastShadow = false;                       // 水は影を落とさない
    Color = Color4(0.2f, 0.5f, 0.9f, 0.5f);   // 半透明の水色
}

bool LiquidCube::IsA(std::string className) {
    if (className == "LiquidCube") return true;
    return BaseCube::IsA(className);
}

void LiquidCube::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "LiquidCube", name, value)) return;
    BaseCube::setProperty(name, value);
}

// 単純な半透明ボックス描画（面デカール処理は不要）
void LiquidCube::draw(int modelLoc, int shaderProgram) {
    glBindVertexArray(Cube::s_VAO);
    static CachedUniform s_colorLocCache;
    static CachedUniform s_uvScaleLocCache;
    static CachedUniform s_isSurfaceGuiLocCache;
    int colorLoc        = cachedUniformLocation(shaderProgram, s_colorLocCache,        "ourColor");
    int uvScaleLoc      = cachedUniformLocation(shaderProgram, s_uvScaleLocCache,      "uvScale");
    int isSurfaceGuiLoc = cachedUniformLocation(shaderProgram, s_isSurfaceGuiLocCache, "isSurfaceGui");
    if (colorLoc        != -1) glUniform4f(colorLoc, Color.r, Color.g, Color.b, Color.a);
    if (uvScaleLoc      != -1) glUniform2f(uvScaleLoc, 1.0f, 1.0f);
    if (isSurfaceGuiLoc != -1) glUniform1f(isSurfaceGuiLoc, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Cube::defaultTextureID);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

std::shared_ptr<Instance> LiquidCube::clone() const {
    auto copy = std::make_shared<LiquidCube>(this->Position, this->Size);
    copy->Name       = this->Name;
    copy->Color      = this->Color;
    copy->Anchored   = this->Anchored;
    copy->CanCollide = this->CanCollide;
    copy->Locked     = this->Locked;
    copy->cframe     = this->cframe;
    copy->material     = this->material;
    copy->MassDensity  = this->MassDensity;
    copy->LockFlags = this->LockFlags;
    copy->CollisionDetection = this->CollisionDetection;
    copy->CastShadow   = this->CastShadow;
    copy->Unlit        = this->Unlit;
    copy->UseTriplanar = this->UseTriplanar;
    copy->TextureScale = this->TextureScale;
    PropertyRegistry::cloneFields(this, copy.get(), "LiquidCube");  // Density
    for (auto const& [name, child] : children)
        copy->addChild(child->clone());
    return copy;
}
