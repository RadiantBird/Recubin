#!/usr/bin/env python3
"""Material プリセット用のテクスチャを手続き的に生成する。

出力: assets/materials/<preset>_<map>.png (512x512, シームレスにタイルする)
  rough_plastic : BaseColor / Roughness / Normal
  wood_planks   : BaseColor / Roughness / Normal
  scratched_metal: BaseColor / Roughness / Metallic / Normal

全パイプラインが非リニアなので、画像は表示空間の値としてそのまま使う。
法線マップは OpenGL 規約(+Y が上、画像の行が増える向きが v 増加)のタンジェント空間。
乱数は固定シードなので、再実行しても同じ画像になる。

使い方: python tools/generate_material_textures.py
"""
import struct
import zlib
from pathlib import Path

import numpy as np

SIZE = 512
OUT_DIR = Path(__file__).resolve().parent.parent / "assets" / "materials"


def write_png(path: Path, rgb: np.ndarray) -> None:
    """uint8 の (H, W, 3) 配列を PNG(RGB, 8bit)として書き出す。標準ライブラリのみ。"""
    height, width, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[row].tobytes() for row in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def gray_to_rgb(values: np.ndarray) -> np.ndarray:
    byte = np.clip(values * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return np.stack([byte, byte, byte], axis=-1)


def color_to_rgb(values: np.ndarray) -> np.ndarray:
    return np.clip(values * 255.0 + 0.5, 0, 255).astype(np.uint8)


def filtered_noise(rng, kx_scale: float, ky_scale: float, power: float = 1.0) -> np.ndarray:
    """周波数領域で整形した周期ノイズ(平均0・標準偏差1)。端が繋がるのでシームレスにタイルする。

    kx_scale / ky_scale が小さいほどその方向に滑らか(低周波)になる。
    """
    white = rng.standard_normal((SIZE, SIZE))
    spectrum = np.fft.fft2(white)
    fy = np.fft.fftfreq(SIZE)[:, None] * SIZE
    fx = np.fft.fftfreq(SIZE)[None, :] * SIZE
    radius = np.sqrt((fx / kx_scale) ** 2 + (fy / ky_scale) ** 2) + 1.0
    result = np.real(np.fft.ifft2(spectrum / radius ** power))
    result -= result.mean()
    return result / (result.std() + 1e-9)


def fbm(rng, base_scale: float, octaves: int = 4) -> np.ndarray:
    total = np.zeros((SIZE, SIZE))
    amplitude = 1.0
    scale = base_scale
    for _ in range(octaves):
        total += amplitude * filtered_noise(rng, scale, scale, 1.6)
        amplitude *= 0.5
        scale *= 2.0
    total -= total.mean()
    return total / (total.std() + 1e-9)


def normal_from_height(height: np.ndarray, strength: float) -> np.ndarray:
    """周期的な高さ場から法線マップを作る。u=列方向、v=行方向(増加方向が上)。"""
    dh_du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dh_dv = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    nx = -dh_du * strength
    ny = -dh_dv * strength
    nz = np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack([nx / length, ny / length, nz / length], axis=-1)
    return color_to_rgb(normal * 0.5 + 0.5)


def splat_lines(rng, count: int, min_length: float, max_length: float, angle_range=None) -> np.ndarray:
    """ランダムな細い線を周期境界で描いた強度マップ(0〜1)。傷に使う。"""
    canvas = np.zeros((SIZE, SIZE))
    for _ in range(count):
        length = rng.uniform(min_length, max_length)
        angle = rng.uniform(*angle_range) if angle_range else rng.uniform(0.0, np.pi)
        x0 = rng.uniform(0, SIZE)
        y0 = rng.uniform(0, SIZE)
        intensity = rng.uniform(0.35, 1.0)
        steps = int(length * 2)
        t = np.linspace(0.0, 1.0, steps)
        xs = x0 + np.cos(angle) * length * t
        ys = y0 + np.sin(angle) * length * t
        fade = np.sin(np.pi * t) ** 0.5  # 両端を細く
        ix = np.floor(xs).astype(int)
        iy = np.floor(ys).astype(int)
        fx = xs - ix
        fy = ys - iy
        for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                          (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
            np.add.at(canvas, ((iy + dy) % SIZE, (ix + dx) % SIZE), w * fade * intensity)
    return np.clip(canvas, 0.0, 1.0)


def blur(values: np.ndarray, sigma: float) -> np.ndarray:
    fy = np.fft.fftfreq(SIZE)[:, None]
    fx = np.fft.fftfreq(SIZE)[None, :]
    gaussian = np.exp(-2.0 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))
    return np.real(np.fft.ifft2(np.fft.fft2(values) * gaussian))


def rough_plastic(rng) -> dict:
    grain = filtered_noise(rng, 90.0, 90.0, 0.7)           # 細かい粒
    mottle = fbm(rng, 2.5, 3)                              # 緩いムラ
    base = 0.90 + 0.025 * mottle
    roughness = 0.72 + 0.10 * mottle + 0.07 * grain
    height = 0.55 * grain + 0.25 * mottle
    return {
        "basecolor": color_to_rgb(np.stack([base, base, base * 1.01], axis=-1)),
        "roughness": gray_to_rgb(np.clip(roughness, 0.40, 0.98)),
        "normal": normal_from_height(height, 1.1),
    }


def wood_planks(rng) -> dict:
    plank_count = 4
    plank_h = SIZE // plank_count
    color = np.zeros((SIZE, SIZE, 3))
    rough = np.zeros((SIZE, SIZE))
    height = np.zeros((SIZE, SIZE))
    base_colors = [(0.58, 0.38, 0.21), (0.50, 0.32, 0.18), (0.63, 0.43, 0.25), (0.54, 0.35, 0.20)]
    for index in range(plank_count):
        y0, y1 = index * plank_h, (index + 1) * plank_h
        # 木目は板に沿って(x方向に)長く、板の幅方向(y)に細かい縞
        warp = filtered_noise(rng, 2.0, 6.0, 1.4)
        fine = filtered_noise(rng, 3.0, 70.0, 0.9)
        yy = np.arange(SIZE)[:, None] / plank_h
        rings = np.sin(2.0 * np.pi * (yy * rng.uniform(5.0, 9.0) + 0.35 * warp))
        grain = 0.55 * rings + 0.35 * fine + 0.25 * warp
        grain = np.roll(grain, int(rng.integers(0, SIZE)), axis=1)  # 板ごとに木目の位置をずらす
        tone = 1.0 + 0.12 * grain
        base = np.array(base_colors[index]) * rng.uniform(0.93, 1.07)
        color[y0:y1] = (base[None, None, :] * tone[y0:y1, :, None])
        rough[y0:y1] = 0.62 - 0.12 * grain[y0:y1]
        height[y0:y1] = 0.30 * grain[y0:y1]
    # 板の継ぎ目(暗く、凹む)
    seam = np.zeros((SIZE, SIZE))
    for index in range(plank_count):
        y = index * plank_h
        for offset, weight in ((-1, 0.5), (0, 1.0), (1, 0.5)):
            seam[(y + offset) % SIZE, :] = np.maximum(seam[(y + offset) % SIZE, :], weight)
    color *= (1.0 - 0.55 * seam)[:, :, None]
    rough = np.clip(rough + 0.30 * seam, 0.35, 0.95)
    height -= 2.0 * seam
    return {
        "basecolor": color_to_rgb(color),
        "roughness": gray_to_rgb(rough),
        "normal": normal_from_height(height, 1.4),
    }


def scratched_metal(rng) -> dict:
    brushed = filtered_noise(rng, 4.0, 140.0, 0.8)         # x方向に伸びたヘアライン
    mottle = fbm(rng, 2.0, 3)
    deep = blur(splat_lines(rng, 140, 60.0, 260.0), 0.7)
    light = blur(splat_lines(rng, 420, 25.0, 110.0), 0.5)
    scratches = np.clip(deep * 1.2 + light * 0.7, 0.0, 1.0)
    base = 0.76 + 0.03 * mottle + 0.015 * brushed
    base = base + 0.10 * scratches                          # 傷は地金が出て明るい
    roughness = 0.26 + 0.05 * mottle + 0.04 * brushed + 0.34 * scratches
    metallic = 0.98 - 0.22 * scratches - 0.03 * np.clip(mottle, 0, None)
    height = 0.12 * brushed - 1.6 * scratches
    return {
        "basecolor": color_to_rgb(np.stack([base, base, base * 1.03], axis=-1)),
        "roughness": gray_to_rgb(np.clip(roughness, 0.12, 0.95)),
        "metallic": gray_to_rgb(np.clip(metallic, 0.0, 1.0)),
        "normal": normal_from_height(height, 1.8),
    }


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    presets = {
        "rough_plastic": rough_plastic(np.random.default_rng(1001)),
        "wood_planks": wood_planks(np.random.default_rng(2002)),
        "scratched_metal": scratched_metal(np.random.default_rng(3003)),
    }
    for preset, maps in presets.items():
        for map_name, rgb in maps.items():
            path = OUT_DIR / f"{preset}_{map_name}.png"
            write_png(path, rgb)
            print(f"wrote {path.relative_to(OUT_DIR.parent.parent)} ({path.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
