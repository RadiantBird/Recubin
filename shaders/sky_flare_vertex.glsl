#version 330 core
// 太陽・月の逆光/光条オーバーレイ用。フルスクリーンクアッド(postprocess用VAOと同じ -1..1)を
// 2通りに使う:
//   uFullscreen = 0: 天体の方向に置いたカメラ向きビルボード（グロー + 光条）
//   uFullscreen = 1: 画面全体（ベール）
layout (location = 0) in vec2 aPos;

uniform float uFullscreen;
uniform mat4  view;
uniform mat4  projection;

// ビルボード（uFullscreen = 0）
uniform vec3  uCenter;     // ワールド座標の中心
uniform vec3  uRight;      // カメラの右・上
uniform vec3  uUp;
uniform float uHalfSize;   // 半幅(stud)

// 画面全体（uFullscreen = 1）。ピクセルごとの視線方向をワールド空間で作る。
uniform vec3  uForward;
uniform float uTanHalfFov;
uniform float uAspect;

out vec2 vLocal;  // ビルボード上の -1..1
out vec3 vRay;    // ピクセルごとの視線方向（ワールド）

void main() {
    vLocal = aPos;
    vRay = uForward
         + uRight * (aPos.x * uTanHalfFov * uAspect)
         + uUp    * (aPos.y * uTanHalfFov);
    if (uFullscreen > 0.5) {
        gl_Position = vec4(aPos, 0.0, 1.0);
    } else {
        vec3 world = uCenter + uRight * (aPos.x * uHalfSize) + uUp * (aPos.y * uHalfSize);
        gl_Position = projection * view * vec4(world, 1.0);
    }
}
