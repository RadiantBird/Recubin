#version 330 core
out vec4 FragColor;

// 頂点シェーダーから送られてくるデータ (in)
in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

// C++側から glUniform... で送られてくるデータ (uniform)
uniform sampler2D ourTexture;
uniform vec4 ourColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

void main() {
    // 1. テクスチャをサンプリング
    vec4 texColor = texture(ourTexture, TexCoord);

    // 2. ライティング計算
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;
    
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    vec3 lighting = ambient + diffuse;

    // 3. 【ここが修正ポイント】
    // mix関数を使って、アルファ値に基づいて「Cubeの色」と「テクスチャの色」を混ぜる
    // texColor.a が 0 なら baseColor = ourColor.rgb
    // texColor.a が 1 なら baseColor = texColor.rgb * ourColor.rgb (色を乗せる)
    vec3 baseColor = mix(ourColor.rgb, texColor.rgb * ourColor.rgb, texColor.a);
    
    // ライティングを最終的な色に適用
    vec3 result = lighting * baseColor;
    
    // 最終的な不透明度は、Cube自体の設定(ourColor.a)に従う
    // これにより、テクスチャが透明な場所でもCubeが消えなくなる
    FragColor = vec4(result, ourColor.a);
}