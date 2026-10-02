# Material

`include/Instances/MaterialInstance.hpp`

`MaterialService` 配下に置く素材定義。`BaseCube` が `Material` プロパティ（パス文字列）で参照すると、PBR 値・物理特性・`Conductive`・6 面テクスチャ投影がこの Material のものになる。`Spatial` ではないので座標系には参加しない。

C++ には物理用の `struct Material`（`include/Util/Material.hpp`）が既にあるため、クラス名は `MaterialInstance`、`getClassName()` と YAML/Luau 上の ClassName は `"Material"`。

## 継承

`Instance` → `Material`（C++ クラス名: `MaterialInstance`）

## プロパティ

| プロパティ | 型 | 既定 | 説明 |
|---|---|---|---|
| `Metallic` | `float` | 0.0 | 金属度（0〜1）。`clampLua()` |
| `Roughness` | `float` | 0.5 | 粗さ（0〜1）。`clampLua()`。シェーダ側で下限 0.045 |
| `Reflectance` | `float` | 0.5 | 非金属の反射率（0〜1）。F0 = 0.16 × Reflectance² |
| `StaticFriction` | `float` | 0.5 | **エディタ/Lua から一時的に非表示**（YAML のみ保存）。Box3D は静摩擦を使わないため将来の実装に備えて保持 |
| `DynamicFriction` | `float` | 0.5 | 動摩擦。Box3D の friction |
| `Restitution` | `float` | 0.1 | 反発 |
| `MassDensity` | `float` | 1.0 | 密度（0 は拒否） |
| `Conductive` | `bool` | false | 雷の標的になるか（従来の `MaterialType::Metal` 判定の置き換え） |

物理系（`StaticFriction`/`DynamicFriction`/`Restitution`/`MassDensity`）は負値・非有限値を拒否してエラーログを出す。値が変わると、参照元 BaseCube の物理 actor を再生成する（`MaterialInstance::notifyPhysicsChanged`）。

## 子インスタンス

`Decal` / `Texture` を子に置くと、この Material を参照する BaseCube の対応する面へ投影される（`Face` は BaseCube のローカル面。Front = ローカル -Z）。優先順位は「BaseCube 直下の Decal/Texture/SurfaceGui/Canvas が面を占有 → 空いた面を Material の子で補う」。同じ面に複数ある場合は従来どおり Decal が Texture に勝つ。MeshCube（UV 空間 Decal）は未対応。

## 参照元の逆引き

`registerUser` / `unregisterUser` / `forEachUser`（`weak_ptr<BaseCube>`）。物理再生成、Decal 選択時の面ハイライト（Material の子 Decal を選ぶと参照元 Cube 全ての面を表示）で使う。

## BaseCube 側の参照

`BaseCube::Material`（パス文字列。YAML の空文字は出力しないため既存シーンは変化しない）。

- 保存形式は `getWorkspaceRelativePath()`（MaterialService 配下なら `MaterialService\<名前>`）。ロード時は全ツリーが揃った `SceneLoader::resolveConstraintRefs` で解決する。
- 解決済みの参照は現在のツリー位置からパスを再計算するので、Material を改名・移動しても保存パスは古くならない。
- 解決できないパスはそのまま保持し、警告を出す。Material が現れた後に `resolveMaterialRef` で後解決できる。
- クローンは解決済みの参照を直接引き継ぐ（クローン範囲外の Material を指すのが通常のため）。
- `effectiveMaterial()` / `effectiveMassDensity()` / `isConductive()`: 参照があれば Material の値、なければ従来の `material` / `MassDensity` / `material.type == Metal`。

## 描画（PBR）

Material を参照する BaseCube だけが PBR で描かれる（未参照は従来の Lambert）。Cook-Torrance GGX（metallic-roughness）、環境反射は Skybox の 6 面から焼いたキューブマップ（`Renderer_Environment.cpp`）。

- 全パイプラインが非リニアのため、PBR も同じ表示空間で計算する。従来 Lambert の拡散（1/π なし）と明るさを揃えるためスペキュラ側に π を掛ける。拡散アンビエントは従来と同じ 0.3 固定で、環境マップは鏡面反射のみに使う。
- PBR 値はインスタンス属性（location 10）で渡すため、Decal/Texture を持たない Material 参照 Cube もインスタンス描画のまま。
- Material の子に Decal/Texture がある場合は面ごとの描画が必要なため、`instanceableShapeIndex` が個別描画へ落とす。

## 依存関係

- `Instance`, `Material`（`include/Util/Material.hpp`）, `PropertyRegistry`, `BaseCube`（前方宣言）
