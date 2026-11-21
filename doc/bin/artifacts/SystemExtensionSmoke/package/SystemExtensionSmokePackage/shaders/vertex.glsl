#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aVertexColor; // Terrain 頂点カラー
layout (location = 4) in float aMatAlpha;   // MeshCube マテリアルアルファ
layout (location = 5) in mat4 aInstModel;  // インスタンス描画時のみ有効(divisor=1)。location 5..8 を占有
layout (location = 9) in vec4 aInstColor;  // インスタンスごとの色

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform float uTime;      // 経過秒（波アニメ用）
uniform float uIsLiquid;  // LiquidCube 描画時 1.0
uniform float uInstanced;                  // 1.0 でインスタンス属性からモデル行列と色を取る

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoord;
out vec4 FragPosLightSpace;
out vec3 VertexColor;
out float MatAlpha;
out vec3 LocalPos;
out vec3 LocalNormal;
out vec4 InstColor;

void main() {
    mat4 m = (uInstanced > 0.5) ? aInstModel : model;
    // LiquidCube は上面のみ時間ベースの sin 波で揺らす（ローカル空間）
    vec3 p = aPos;
    if (uIsLiquid > 0.5 && aPos.y > 0.0) {
        const float WAVE_ANGULAR_SPEED = 1.5;
        const float WAVE_SPATIAL_FREQUENCY = 4.0;
        const float WAVE_AMPLITUDE = 0.06;
        p.y += sin(uTime * WAVE_ANGULAR_SPEED +
                   aPos.x * WAVE_SPATIAL_FREQUENCY +
                   aPos.z * WAVE_SPATIAL_FREQUENCY) * WAVE_AMPLITUDE;
    }
    FragPos = vec3(m * vec4(p, 1.0));
    Normal = mat3(transpose(inverse(m))) * aNormal;
    TexCoord = aTexCoord;
    VertexColor = aVertexColor;
    MatAlpha = aMatAlpha;
    LocalPos = p;
    LocalNormal = aNormal;
    InstColor = (uInstanced > 0.5) ? aInstColor : vec4(1.0);
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
