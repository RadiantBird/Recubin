#pragma once
#include <cmath>
#include <cstdint>

// ================================================================== //
//  PerlinNoise  — ヘッダーオンリー実装
//
//  使い方:
//    PerlinNoise noise(seed);
//    float h = noise.fbm2(worldX, worldZ, octaves, persistence, lacunarity);
//
//  fbm2 の戻り値はおおむね [-1, 1]（オクターブ・persistence により変動）
// ================================================================== //

class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed = 0) { reseed(seed); }

    void reseed(uint32_t seed) {
        // 順列テーブルを LCG でシャッフル
        for (int i = 0; i < 256; i++) p[i] = i;
        for (int i = 255; i > 0; i--) {
            seed = seed * 1664525u + 1013904223u; // LCG
            int j = seed % (i + 1);
            int tmp = p[i]; p[i] = p[j]; p[j] = tmp;
        }
        for (int i = 0; i < 256; i++) p[256 + i] = p[i];
    }

    // 2D Perlin ノイズ（戻り値: [-1, 1] 近似）
    float noise2(float x, float y) const {
        int xi = fastFloor(x) & 255;
        int yi = fastFloor(y) & 255;
        float xf = x - fastFloor(x);
        float yf = y - fastFloor(y);
        float u = fade(xf), v = fade(yf);

        int aa = p[p[xi  ] + yi  ];
        int ab = p[p[xi  ] + yi+1];
        int ba = p[p[xi+1] + yi  ];
        int bb = p[p[xi+1] + yi+1];

        float r = lerp(v,
            lerp(u, grad2(aa, xf,   yf  ),
                    grad2(ba, xf-1, yf  )),
            lerp(u, grad2(ab, xf,   yf-1),
                    grad2(bb, xf-1, yf-1)));
        return r;
    }

    // fBm（fractional Brownian motion）2D
    //   octaves     : 重ね合わせ回数（4〜8 が一般的）
    //   persistence : 各オクターブの振幅倍率（0.5 が標準）
    //   lacunarity  : 各オクターブの周波数倍率（2.0 が標準）
    // 戻り値: おおむね [-1, 1]
    float fbm2(float x, float y,
               int   octaves     = 6,
               float persistence = 0.5f,
               float lacunarity  = 2.0f) const
    {
        float value     = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue  = 0.0f; // 正規化用

        for (int i = 0; i < octaves; i++) {
            value    += noise2(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        return value / maxValue; // [-1, 1] に正規化
    }

private:
    int p[512];

    static int fastFloor(float x) {
        return x >= 0 ? (int)x : (int)x - 1;
    }
    static float fade(float t) {
        return t * t * t * (t * (t * 6 - 15) + 10); // 6t^5 - 15t^4 + 10t^3
    }
    static float lerp(float t, float a, float b) {
        return a + t * (b - a);
    }
    static float grad2(int hash, float x, float y) {
        // 下位2ビットで4方向のグラジェント
        switch (hash & 3) {
            case 0: return  x + y;
            case 1: return -x + y;
            case 2: return  x - y;
            case 3: return -x - y;
            default: return 0;
        }
    }
};