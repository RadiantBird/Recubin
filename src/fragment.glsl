#version 330 core

// 1. バーテックスシェーダーの "out" と同じ名前で受け取る
in vec3 Normal;
in vec3 FragPos;

// 2. C++ 側から glUniform... で送るデータ
uniform vec4 ourColor;   // これが抜けていたのでエラー！
uniform vec3 lightPos;
uniform vec3 lightColor;

// 3. 最終的な出力先
out vec4 FragColor;      // これも宣言しないと色が出せません！

void main() {
    // 1. 周囲の明るさ (Ambient)
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    // 2. 直接当たる光 (Diffuse)
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // 3. 最終的な色
    vec3 result = (ambient + diffuse) * ourColor.rgb;
    FragColor = vec4(result, ourColor.a);
}