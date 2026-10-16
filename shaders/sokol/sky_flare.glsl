// 太陽・月のオーバーレイ（Renderer::renderSkyFlares）
//
// 頂点シェーダー:
// 太陽・月のオーバーレイ（逆光・光条・月の満ち欠け）用。フルスクリーンクアッド(postprocess用VAOと
// 同じ -1..1)を2通りに使う:
//   uFullscreen = 0: 天体の方向に置いたカメラ向きビルボード（グロー + 光条 / 月の円盤）
//   uFullscreen = 1: 画面全体（ベール）
// ビルボード・画面全体のどちらでも、天体の円盤が手前の物体にどれだけ隠れていないか(vVisibility)を
// シーンの深度テクスチャから求める。
//
// フラグメントシェーダー:
// 太陽・月のオーバーレイ。モードは3つ（uFullscreen / uPhaseDisc で切り替える）:
//   ベール        (uFullscreen = 1): 画面全体を白く霞ませる（加算）
//   グロー + 光条 (既定)           : 天体の周りの光のにじみと放射状の筋（加算）
//   月の円盤      (uPhaseDisc = 1) : 満ち欠けのある月（通常のアルファブレンド）
// グロー・光条・月の円盤は、シーンの深度テクスチャで手前の物体に遮られる。
//
// sokol では 1 つのテクスチャを頂点・フラグメントの両方へ束縛できないので、シーンの深度テクスチャは
// 頂点用(uSceneDepthVs)とフラグメント用(uSceneDepth)の 2 つのスロットに同じものを束縛する。
// 遮蔽判定の値(uFullscreen, uOcclusionEnabled, uProjA, uProjB, uOccluderDepth)も、ユニフォーム
// ブロックがステージごとなので両方のブロックに持つ。
@module sky

@vs flare_vs
layout(binding = 0) uniform flare_vs_params {
    mat4  view;
    mat4  projection;
    // ビルボード（uFullscreen = 0）
    vec3  uCenter;     // ワールド座標の中心（天体の方向 × 描画距離）。画面全体のときも設定する
    float uHalfSize;   // 半幅(stud)
    vec3  uRight;      // カメラの右・上
    float uTanHalfFov;
    vec3  uUp;
    float uAspect;
    // 画面全体（uFullscreen = 1）。ピクセルごとの視線方向をワールド空間で作る。
    vec3  uForward;
    float uFullscreen;
    // 遮蔽判定
    float uOcclusionEnabled;  // 0: 深度が使えない（遮蔽なしとして描く）
    float uProjA;             // 投影行列の m[10] / m[14]。深度を視線方向の距離へ戻すのに使う
    float uProjB;
    float uOccluderDepth;     // これより手前(視線方向の距離, stud)の物体を「天体を隠す」とみなす
    float uDiscRadius;        // 天体の円盤の世界での半径(stud)
};

// 遮蔽判定。不透明物の描画後にビューポートの深度をコピーしたテクスチャ。
layout(binding = 0) uniform texture2D uSceneDepthVs;
layout(binding = 0) uniform sampler uSceneDepthVsSampler;

layout(location = 0) in vec2 aPos;

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
            float depth = textureLod(sampler2D(uSceneDepthVs, uSceneDepthVsSampler), s * 0.5 + 0.5, 0.0).r;
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
@end

@fs flare_fs
layout(binding = 1) uniform flare_fs_params {
    vec3  uColor;            // 天体の色
    float uStrength;         // 全体の強さ（グロー/光条/ベール: Color.a と地平線フェードの積、月の円盤: Color.a）
    vec3  uBodyDirection;    // 天体の方向（ワールド、単位ベクトル）。ベールで使う
    float uFullscreen;       // 1: ベール
    vec2  uViewportSize;     // 深度テクスチャの大きさ(ピクセル)
    float uPhaseDisc;        // 1: 月の円盤
    float uBleed;            // 手前の物体の上へのグローのにじみ。天体が見えている分だけ効く
    // 遮蔽判定（頂点シェーダーと同じ値）
    float uOcclusionEnabled;
    float uProjA;
    float uProjB;
    float uOccluderDepth;
    // グロー + 光条。座標は「円盤の半径を1とした中心からの距離」で扱う。
    float uExtent;           // ビルボードの半幅が円盤半径の何倍か
    float uGlowIntensity;
    float uGlowRadius;
    float uSpikeIntensity;
    float uSpikeLength;
    float uSpikeRotation;    // ラジアン
    float uSpikeCount;       // 本数（整数値）。GL ではブロックを vec4 配列にするため float で持つ
    // ベール
    float uVeilIntensity;
    float uVeilFalloff;
    // 月の満ち欠け
    float uPhase;            // 0..1（0 = 新月、0.25 = 上弦、0.5 = 満月、0.75 = 下弦）
    float uPhaseRotation;    // 明暗境界の向き(ラジアン)
    float uEarthshine;       // 影の部分の見え方（0 = 透明）
};

layout(binding = 1) uniform texture2D uSceneDepth;
layout(binding = 1) uniform sampler uSceneDepthSampler;

in vec2 vLocal;
in vec3 vRay;
flat in float vVisibility;  // 天体の円盤が隠れていない割合（頂点シェーダーで算出）
out vec4 FragColor;

const float PI = 3.14159265;

// 1ピクセルが手前の物体に隠れていれば1。深度を視線方向の距離へ戻して uOccluderDepth と比べる。
float sceneOccluded(vec2 uv) {
    float depth = texture(sampler2D(uSceneDepth, uSceneDepthSampler), uv).r;
    return (uProjB / (depth * 2.0 - 1.0 + uProjA)) < uOccluderDepth ? 1.0 : 0.0;
}

// この画素の遮蔽率 0..1（5点の平均で縁を柔らかくする）。深度が使えないときは0。
float occlusionAt() {
    if (uOcclusionEnabled < 0.5) return 0.0;
    vec2 uv = gl_FragCoord.xy / uViewportSize;
    vec2 px = vec2(2.0) / uViewportSize;
    return (sceneOccluded(uv)
          + sceneOccluded(uv + vec2(px.x, 0.0))
          + sceneOccluded(uv - vec2(px.x, 0.0))
          + sceneOccluded(uv + vec2(0.0, px.y))
          + sceneOccluded(uv - vec2(0.0, px.y))) * 0.2;
}

// グロー: 円盤の縁から外へ滑らかに減衰し、uGlowRadius で0になる（クアッドの端で切れない）。
float glowAt(float r) {
    float falloff = 1.0 - smoothstep(0.0, uGlowRadius, r);
    return uGlowIntensity * falloff * falloff;
}

// 光条: uSpikeCount 本の筋が等間隔で放射する。fragment の極角を最も近い筋との角度差へ
// 折り返し、筋に沿った距離(along)と筋からの垂直距離(across)で形を作る。筋は先へ行くほど細く、暗くなる。
float spikesAt(vec2 local, float r) {
    float count = max(uSpikeCount, 2.0);
    float sector = 2.0 * PI / count;
    float angle = atan(local.y, local.x) - uSpikeRotation;
    float folded = abs(mod(angle + 0.5 * sector, sector) - 0.5 * sector);  // [0, sector/2]
    float along = r * cos(folded);
    float across = r * sin(folded);
    float t = clamp(along / uSpikeLength, 0.0, 1.0);
    float sigma = 0.012 + 0.07 * (1.0 - t);
    float beam = exp(-(across * across) / (2.0 * sigma * sigma));
    float fade = (1.0 - t) * (1.0 - t) / (1.0 + 0.15 * along);
    return uSpikeIntensity * beam * fade;
}

void main() {
    if (uFullscreen > 0.5) {
        // ベール: カメラが天体を向くほど画面全体が白く霞む。天体が手前の物体に隠れているほど弱める。
        // 色は白寄りにして霞らしくする。
        float facing = max(dot(normalize(vRay), uBodyDirection), 0.0);
        float veil = uVeilIntensity * pow(facing, uVeilFalloff) * uStrength * vVisibility;
        vec3 tint = mix(vec3(1.0), uColor, 0.5);
        FragColor = vec4(tint * veil, 1.0);
        return;
    }

    if (uPhaseDisc > 0.5) {
        // 月の円盤。カメラ向きの球として明暗を付ける。座標系は x=右、y=上、z=手前。
        float r2 = dot(vLocal, vLocal);
        if (r2 >= 1.0) discard;
        float r = sqrt(r2);
        vec3 normal = vec3(vLocal, sqrt(max(1.0 - r2, 0.0)));

        // 光の向き: 新月は奥から、上弦は右から、満月は手前から、下弦は左から当たる。
        float phaseAngle = 2.0 * PI * uPhase;
        vec3 light = vec3(sin(phaseAngle), 0.0, -cos(phaseAngle));
        float c = cos(uPhaseRotation);
        float s = sin(uPhaseRotation);
        light.xy = vec2(c * light.x - s * light.y, s * light.x + c * light.y);

        float lit = smoothstep(-0.04, 0.04, dot(normal, light));  // 明暗境界(terminator)
        vec3 color = mix(uColor * 0.35, uColor, lit);             // 影は暗く(地球照)
        float edge = 1.0 - smoothstep(0.97, 1.0, r);              // 縁のジャギーを抑える
        float alpha = mix(uEarthshine, 1.0, lit) * edge * uStrength * (1.0 - occlusionAt());
        FragColor = vec4(color, alpha);
        return;
    }

    float r = length(vLocal) * uExtent;
    if (r >= uExtent) discard;  // 角を落として円形にする

    // 光条は物体を貫通しない。グローは物体の上へも少しにじむが、天体が隠れているほど弱い。
    float occlusion = occlusionAt();
    float glowVisibility = (1.0 - occlusion) + occlusion * uBleed * vVisibility;
    float spikeVisibility = 1.0 - occlusion;

    float light = 0.0;
    if (uGlowIntensity > 0.0)  light += glowAt(r) * glowVisibility;
    if (uSpikeIntensity > 0.0) light += spikesAt(vLocal, r) * spikeVisibility;
    FragColor = vec4(uColor * light * uStrength, 1.0);
}
@end

@program flare flare_vs flare_fs
