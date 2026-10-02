#version 330 core
// 環境キューブマップのプレフィルタ。ソースキューブマップをGGX重要度サンプリングで
// roughnessごとにぼかし、mipレベルmに roughness = m / (mipCount - 1) の結果を書く。
// 頂点シェーダーは postprocess_vertex.glsl（フルスクリーン）を共用する。
out vec4 FragColor;

uniform samplerCube uSrc;
uniform float uRoughness;
uniform int   uFace;      // 書き込み中のキューブ面 (0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z)
uniform float uSize;      // 書き込み先mipの一辺のピクセル数
uniform float uSrcSize;   // ソースmip0の一辺のピクセル数（サンプルのmip選択に使う）

const float PI = 3.14159265;
const uint SAMPLE_COUNT = 128u;

// キューブ面のピクセル座標から方向を求める（OpenGL仕様のキューブマップ面規約）
vec3 faceDirection(int face, vec2 uv) {
    float u = uv.x * 2.0 - 1.0;
    float v = uv.y * 2.0 - 1.0;
    if (face == 0) return vec3( 1.0, -v, -u);
    if (face == 1) return vec3(-1.0, -v,  u);
    if (face == 2) return vec3(   u, 1.0,  v);
    if (face == 3) return vec3(   u, -1.0, -v);
    if (face == 4) return vec3(   u,  -v, 1.0);
    return vec3(-u, -v, -1.0);
}

float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

void main() {
    vec2 uv = gl_FragCoord.xy / uSize;
    vec3 N = normalize(faceDirection(uFace, uv));

    if (uRoughness < 0.001) {
        FragColor = vec4(textureLod(uSrc, N, 0.0).rgb, 1.0);
        return;
    }

    vec3 V = N;
    vec3 sum = vec3(0.0);
    float totalWeight = 0.0;
    // サンプル1つがソース上で覆うテクセル面積から、ぼかし済みmipを選んでノイズを抑える
    float saTexel = 4.0 * PI / (6.0 * uSrcSize * uSrcSize);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);
        float pdf = distributionGGX(NdotH, uRoughness) * NdotH / (4.0 * HdotV) + 1e-4;
        float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 1e-4);
        float lod = 0.5 * log2(saSample / saTexel);
        sum += textureLod(uSrc, L, max(lod, 0.0)).rgb * NdotL;
        totalWeight += NdotL;
    }
    FragColor = vec4(sum / max(totalWeight, 1e-4), 1.0);
}
