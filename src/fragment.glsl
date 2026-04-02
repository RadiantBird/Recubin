#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform sampler2D ourTexture;
uniform vec4 ourColor;
uniform vec3 lightPos;  // 追加：光源の位置 (World座標)
uniform vec3 viewPos;   // 追加：カメラの位置 (World座標)
uniform vec3 lightColor; // 追加：光の色 (例: vec3(1.0))

void main() {
    // 1. 環境光 (Ambient) - 暗闇でもうっすら見える程度
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * lightColor;

    // 2. 拡散反射 (Diffuse) - 面の向きと光の方向で明るさを決める
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // テクスチャの色を取得
    vec4 texColor = texture(ourTexture, TexCoord);

    // 最終的な色：(環境光 + 拡散反射) * テクスチャ * 基本色
    vec3 result = (ambient + diffuse) * vec3(texColor) * vec3(ourColor);
    
    FragColor = vec4(result, 1.0);
}