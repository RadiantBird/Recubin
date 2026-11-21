#include <Instances/LiquidCube.hpp>
#include <Instances/Cube.hpp>          // s_VAO / defaultTextureID 流用
#include <include/Core/PropertyRegistry.hpp>
#include <Core/MainShaderState.hpp>
#include <GL/glew.h>
#include <cmath>

static const bool s_liquidCubeRegistered = []{
    using namespace PropertyRegistry;
    registerClass("LiquidCube", "BaseCube", {
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

void LiquidCube::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "LiquidCube", name, value)) return;
    BaseCube::setProperty(name, value);
}

// 単純な半透明ボックス描画（面デカール処理は不要）
void LiquidCube::draw(MainShaderState& shader) {
    glBindVertexArray(Cube::s_VAO);
    shader.setColor(Color4(Color.r, Color.g, Color.b, Color.a));
    shader.setUvScale(1.0f, 1.0f);
    shader.setSurfaceGui(0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Cube::defaultTextureID.glName());
    shader.flush();
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}

std::shared_ptr<Instance> LiquidCube::clone() const {
    auto copy = std::make_shared<LiquidCube>(this->getPosition(), this->Size);
    PropertyRegistry::cloneFields(this, copy.get(), "LiquidCube");  // Density
    cloneBaseCubeStateAndChildrenTo(copy);
    return copy;
}
