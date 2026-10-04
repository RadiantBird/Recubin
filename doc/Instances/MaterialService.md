# MaterialService

`include/Instances/MaterialService.hpp`

`System` 直下に自動生成されるサービス。配下に [Material](Material.md) を置く。自身はプロパティを持たないコンテナ。

## 継承

`Instance` → `MaterialService`

## 生成・保存

- `SceneRuntime::commitAndBind` が、読み込んだシーンに無ければ System 直下へ自動生成する（古いシーンの互換と、BaseCube の Material 参照解決の前提）。
- YAML には通常のインスタンスとして保存される（`Properties` は空のため出力されない）。
- `SceneLoader::createInstance` に登録済み。Explorer の Insert Object には載せない（シングルトン）。`Instance.new` でも生成不可。
- Luau からは `System.MaterialService` の子名参照でアクセスする（PathfindingService と同じ方式）。

## プリセットの追加

Explorer で MaterialService を右クリック →「マテリアルプリセットを追加」から、画像マップと値を割り当て済みの [Material](Material.md) を追加できる（ざらついたプラスチック／木の板／傷のある金属）。作られる Material は子に `FileRef`（`BaseColor`/`Roughness`/`Metallic`/`Normal`）を持ち、同名の参照プロパティからそれを指す。名前が重複した場合は連番が付き、Undo で追加を取り消せる。

## 親子の制約

親子の許可ルールは存在しないため、Material を MaterialService 以外に置くこともできる（参照パスは `getWorkspaceRelativePath()` で解決される）。

## 継承クラス

なし
