#version 330 core
// 太陽・月のオーバーレイ（逆光・光条・月の満ち欠け）用。フルスクリーンクアッド(postprocess用VAOと
// 同じ -1..1)を2通りに使う:
//   uFullscreen = 0: 天体の方向に置いたカメラ向きビルボード（グロー + 光条 / 月の円盤）
//   uFullscreen = 1: 画面全体（ベール）
// ビルボード・画面全体のどちらでも、天体の円盤が手前の物体にどれだけ隠れていないか(vVisibility)を
// シーンの深度テクスチャから求める。
layout (location = 0) in vec2 aPos;

uniform float uFullscreen;
uniform mat4  view;
uniform mat4  projection;

// ビルボード（uFullscreen = 0）
uniform vec3  uCenter;     // ワールド座標の中心（天体の方向 × 描画距離）。画面全体のときも設定する
uniform vec3  uRight;      // カメラの右・上
uniform vec3  uUp;
uniform float uHalfSize;   // 半幅(stud)

// 画面全体（uFullscreen = 1）。ピクセルごとの視線方向をワールド空間で作る。
uniform vec3  uForward;
uniform float uTanHalfFov;
uniform float uAspect;

// 遮蔽判定。uSceneDepth は不透明物の描画後にビューポートの深度をコピーしたテクスチャ。
uniform sampler2D uSceneDepth;
uniform float uOcclusionEnabled;  // 0: 深度が使えない（遮蔽なしとして描く）
uniform float uProjA;             // 投影行列の m[10] / m[14]。深度を視線方向の距離へ戻すのに使う
uniform float uProjB;
uniform float uOccluderDepth;     // これより手前(視線方向の距離, stud)の物体を「天体を隠す」とみなす
uniform float uDiscRadius;        // 天体の円盤の世界での半径(stud)

out vec2 vLocal;                  // ビルボード上の -1..1
out vec3 vRay;                    // ピクセルごとの視線方向（ワールド）
flat out float vVisibility;       // 天体の円盤が隠れていない割合 0..1

// 深度バッファの値(0..1)を、視線方向の距離(stud)へ戻す。
float planarDepth(float depth) {
    return uProjB / (depth * 2.0 - 1.0 + uProjA);
}

// 天体の円盤の範囲を5x5でサンプルし、手前の物体に隠れていない点の割合を返す。
// 画面外の点は深度が分からないので見えている扱い。
float discVisibility() {
    if (uOcclusionEnabled < 0.5) return 1.0;
    mat4 viewProj = projection * view;
    vec4 c = viewProj * vec4(uCenter, 1.0);
    vec4 e = viewProj * vec4(uCenter + uRight * uDiscRadius, 1.0);
    vec4 t = viewProj * vec4(uCenter + uUp * uDiscRadius, 1.0);
    if (c.w <= 0.0 || e.w <= 0.0 || t.w <= 0.0) return 1.0;
    vec2 ndc = c.xy / c.w;
    vec2 radiusNdc = vec2(abs(e.x / e.w - ndc.x), abs(t.y / t.w - ndc.y));
    float visible = 0.0;
    float total = 0.0;
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            vec2 offset = vec2(float(i), float(j)) * 0.5;
            if (dot(offset, offset) > 1.0) continue;  // 円の外の角は使わない
            vec2 s = ndc + offset * radiusNdc;
            total += 1.0;
            if (abs(s.x) > 1.0 || abs(s.y) > 1.0) { visible += 1.0; continue; }
            float depth = textureLod(uSceneDepth, s * 0.5 + 0.5, 0.0).r;
            if (planarDepth(depth) >= uOccluderDepth) visible += 1.0;
        }
    }
    return visible / max(total, 1.0);
}

void main() {
    vLocal = aPos;
    vRay = uForward
         + uRight * (aPos.x * uTanHalfFov * uAspect)
         + uUp    * (aPos.y * uTanHalfFov);
    vVisibility = discVisibility();
    if (uFullscreen > 0.5) {
        gl_Position = vec4(aPos, 0.0, 1.0);
    } else {
        vec3 world = uCenter + uRight * (aPos.x * uHalfSize) + uUp * (aPos.y * uHalfSize);
        gl_Position = projection * view * vec4(world, 1.0);
    }
}
