#version 330 core
// 太陽・月のオーバーレイ。モードは3つ（uFullscreen / uPhaseDisc で切り替える）:
//   ベール        (uFullscreen = 1): 画面全体を白く霞ませる（加算）
//   グロー + 光条 (既定)           : 天体の周りの光のにじみと放射状の筋（加算）
//   月の円盤      (uPhaseDisc = 1) : 満ち欠けのある月（通常のアルファブレンド）
// グロー・光条・月の円盤は、シーンの深度テクスチャで手前の物体に遮られる。
in vec2 vLocal;
in vec3 vRay;
flat in float vVisibility;  // 天体の円盤が隠れていない割合（頂点シェーダーで算出）
out vec4 FragColor;

uniform float uFullscreen;       // 1: ベール
uniform float uPhaseDisc;        // 1: 月の円盤
uniform vec3  uColor;            // 天体の色
uniform float uStrength;         // 全体の強さ（グロー/光条/ベール: Color.a と地平線フェードの積、月の円盤: Color.a）

// 遮蔽判定（頂点シェーダーと同じ。GLSLは #include が使えないため関数を重複して持つ）
uniform sampler2D uSceneDepth;
uniform float uOcclusionEnabled;
uniform float uProjA;
uniform float uProjB;
uniform float uOccluderDepth;
uniform vec2  uViewportSize;     // 深度テクスチャの大きさ(ピクセル)
uniform float uBleed;            // 手前の物体の上へのグローのにじみ。天体が見えている分だけ効く

// グロー + 光条。座標は「円盤の半径を1とした中心からの距離」で扱う。
uniform float uExtent;           // ビルボードの半幅が円盤半径の何倍か
uniform float uGlowIntensity;
uniform float uGlowRadius;
uniform float uSpikeIntensity;
uniform float uSpikeLength;
uniform float uSpikeRotation;    // ラジアン
uniform int   uSpikeCount;

// ベール
uniform float uVeilIntensity;
uniform float uVeilFalloff;
uniform vec3  uBodyDirection;    // 天体の方向（ワールド、単位ベクトル）

// 月の満ち欠け
uniform float uPhase;            // 0..1（0 = 新月、0.25 = 上弦、0.5 = 満月、0.75 = 下弦）
uniform float uPhaseRotation;    // 明暗境界の向き(ラジアン)
uniform float uEarthshine;       // 影の部分の見え方（0 = 透明）

const float PI = 3.14159265;

// 1ピクセルが手前の物体に隠れていれば1。深度を視線方向の距離へ戻して uOccluderDepth と比べる。
float sceneOccluded(vec2 uv) {
    float depth = texture(uSceneDepth, uv).r;
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
    float count = float(max(uSpikeCount, 2));
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
