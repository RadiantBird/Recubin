#version 330 core
// 太陽・月の逆光(グロー + ベール)と光条(回折スパイク)。加算ブレンドで重ねる前提で、
// 出力は「足す光」(rgb)。アルファは常に1。
in vec2 vLocal;
in vec3 vRay;
out vec4 FragColor;

uniform float uFullscreen;       // 1: ベール、0: グロー + 光条
uniform vec3  uColor;            // 天体の色
uniform float uStrength;         // 全体の強さ（Color.a と地平線フェードの積）

// ビルボード側。座標は「円盤の半径を1とした中心からの距離」で扱う。
uniform float uExtent;           // ビルボードの半幅が円盤半径の何倍か
uniform float uGlowIntensity;
uniform float uGlowRadius;
uniform float uSpikeIntensity;
uniform float uSpikeLength;
uniform float uSpikeRotation;    // ラジアン
uniform int   uSpikeCount;

// ベール側
uniform float uVeilIntensity;
uniform float uVeilFalloff;
uniform vec3  uBodyDirection;    // 天体の方向（ワールド、単位ベクトル）

const float PI = 3.14159265;

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
        // ベール: カメラが天体を向くほど画面全体が白く霞む。色は白寄りにして霞らしくする。
        float facing = max(dot(normalize(vRay), uBodyDirection), 0.0);
        float veil = uVeilIntensity * pow(facing, uVeilFalloff) * uStrength;
        vec3 tint = mix(vec3(1.0), uColor, 0.5);
        FragColor = vec4(tint * veil, 1.0);
        return;
    }

    float r = length(vLocal) * uExtent;
    if (r >= uExtent) discard;  // 角を落として円形にする

    float light = 0.0;
    if (uGlowIntensity > 0.0)  light += glowAt(r);
    if (uSpikeIntensity > 0.0) light += spikesAt(vLocal, r);
    FragColor = vec4(uColor * light * uStrength, 1.0);
}
