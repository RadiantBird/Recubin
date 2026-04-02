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

    vec3 result = lighting * texColor.rgb * ourColor.rgb;
    
    FragColor = vec4(result, texColor.a * ourColor.a);
}