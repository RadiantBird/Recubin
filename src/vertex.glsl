#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aVertexColor; // Terrain 頂点カラー
layout (location = 4) in float aMatAlpha;   // MeshCube マテリアルアルファ

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform float uTime;      // 経過秒（波アニメ用）
uniform float uIsLiquid;  // LiquidCube 描画時 1.0

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoord;
out vec4 FragPosLightSpace;
out vec3 VertexColor;
out float MatAlpha;
out vec3 LocalPos;
out vec3 LocalNormal;

void main() {
    // LiquidCube は上面のみ時間ベースの sin 波で揺らす（ローカル空間）
    vec3 p = aPos;
    if (uIsLiquid > 0.5 && aPos.y > 0.0) {
        p.y += sin(uTime * 1.5 + aPos.x * 4.0 + aPos.z * 4.0) * 0.06;
    }
    FragPos = vec3(model * vec4(p, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    VertexColor = aVertexColor;
    MatAlpha = aMatAlpha;
    LocalPos = p;
    LocalNormal = aNormal;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = projection * view * vec4(FragPos, 1.0);
}