#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform sampler2D ourTexture;
uniform vec4 ourColor;   // C++から送る色
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

void main() {
    // 1. テクスチャをサンプリング
    vec4 texColor = texture(ourTexture, TexCoord);

    // 【ここがポイント】
    // テクスチャが貼られていない、あるいは真っ黒な場合、
    // lighting * 0 * ourColor = 0 になるのを防ぐため 1.0 に置き換える
    vec3 effectiveTexColor = texColor.rgb;
    if (length(texColor.rgb) < 0.01) {
        effectiveTexColor = vec3(1.0);
    }

    // 2. ライティング計算
    float ambientStrength = 0.3; 
    vec3 ambient = ambientStrength * lightColor;
    
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // 3. 最終色の合成
    vec3 lighting = ambient + diffuse;
    
    // 【修正】テクスチャがない（texColorが真っ黒、またはバインドされていない）面でも
    // Cubeの色が出るように「テクスチャの色」と「Cubeの色」を適切に混ぜる
    // もしテクスチャが(0,0,0)なら、ourColorだけが出るように調整
    vec3 baseColor = (texColor.rgb == vec3(0.0)) ? ourColor.rgb : texColor.rgb * ourColor.rgb;
    
    vec3 result = lighting * baseColor;
    
    FragColor = vec4(result, 1.0); 
}