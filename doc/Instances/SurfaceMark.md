# SurfaceMark

`SurfaceMark` は `Spatial` を継承し、親の `BaseCube` に所属せず、3D空間上の位置・回転・サイズから画像を表面へ投影するインスタンスです。

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Position` | `Vector3` | 投影開始位置（`Spatial`） |
| `Rotation` | `Quaternion` | 投影向き（`Spatial`） |
| `Size` | `Vector3` | X=幅、Y=高さ、Z=投影深さ（原点をnear planeとしてローカルZ=[-深さ,0]） |
| `Color` | `Color4` | 投影画像の色・アルファ |
| `Texture` | `string` | 画像パス（空の場合は保存しない） |
| `FilterMode` | `"Exclude"` / `"Include"` | 投影対象の選択方式 |
| `FilterInstances` | `Instance[]` | 登録Instance自身と子孫BaseCubeを対象にする参照配列 |

ローカル `-Z` 軸が投影 Forward です。原点が投影のnear planeで、投影volumeはローカルZ=[-Size.z, 0]です。投影領域内の各位置で最初に接触する表面へ投影するため、隣接する面や複数の `BaseCube` をまたげます。

`TexturePath` と `TextureID` は Luau から読み取りできます。画像の設定は `Source` に `FileRef` を代入します。

`FilterMode` は既定値が `Exclude` です。空のExcludeは全BaseCubeを許可し、空のIncludeは全て拒否します。`FilterInstances` はWorkspace相対パスで保存され、ModelやFolderを指定すると子孫BaseCubeも一致します。Exclude対象は深度生成から除外されるため、その奥の許可対象へ投影が通ります。解決できないパスは保存・編集用に保持され、描画時だけ無視されます。

```luau
mark.FilterMode = "Include"
mark.FilterInstances = { workspace.Wall, workspace.Decals }
```
