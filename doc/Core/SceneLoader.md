# SceneLoader

`include/Core/SceneLoader.hpp`

YAML シーンファイルを読み込んで Instance ツリーを再構築する静的ファクトリクラス。

## メソッド（すべて static）

| メソッド | 説明 |
|---|---|
| `loadScene(filePath)` | YAML ファイルをパースしてルート Instance を返す |
| `loadScene(filePath, context)` | 呼び出し単位の `LoadContext` に登録された既存Instanceへ対象クラスをマージして読み込む |
| `loadSceneResult(filePath)` | `LoadResult`（状態、メッセージ、`SceneDocumentMetadata`）付きで読み込む。ヘッダーなしはscene version 0 |
| `loadSceneResult(filePath, context)` | `LoadContext` を使うResult付きロード |
| `serializeSceneResult(root, metadata)` | ファイルを開かずScene YAMLを文字列として生成する |
| `saveSceneResult(root, filePath, metadata)` | SceneヘッダーとCharacter Animation参照移行versionを保存する。失敗はfalse |
| `parseInstance(node, context)` | `LoadContext` を伝播しながらYAMLノードを再帰的にInstanceへ変換（内部API） |
| `createInstance(className)` | クラス名文字列から Instance を new して返すファクトリ |

## LoadContext

`LoadContext` はクラス名と既存Instanceの対応を1回のロード呼び出しだけに適用する。`System`や
`User`のように新規生成せず既存オブジェクトへプロパティと子をマージする対象を
`registerMergeInstance(className, instance)` で登録する。Contextなしのロードには登録が存在せず、
通常クラスだけをファクトリ生成する。登録内容を保持するプロセス全体の静的状態はないため、
失敗後や別ロードへマージ対象が漏れない。

Scene YAMLの`Position`と`Rotation`は各Spatialのlocal座標として扱う。通常の`addChild()`は
reparent時にworld姿勢を維持するため、SceneLoaderは子サブツリー内の全Spatial（Folderなどの
非Spatial中間ノード配下を含む）のlocal CFrameを接続前に収集し、接続後に
`Spatial::applyLocalCFrameBatch()`で復元する。この復元はdeserialization時の接続だけに限定し、
通常のInstance reparent semanticsは変更しない。

## 対応クラス（createInstance）

`Instance`, `Spatial`, `BaseCube`, `Cube`, `Script`, `Sound`, `Decal`, `Model`, `Workspace`

## シーンロードフロー

```
SceneLoader::loadScene("assets/scenes/test_scene.yaml", context)
  → YAML ドキュメントをパース
  → parseInstance(rootNode)
       → context対象なら既存Instance、その他はcreateInstance(className)
       → node のプロパティを setProperty() で適用
       → 子ノードを再帰的に parseInstance()
       → addChild() で親子関係を構築
            → 接続前に子サブツリーのlocal CFrameを収集
            → onAncestorChanged() が伝播
            → 接続後にdeserialization batchでlocal CFrameを復元
  → Workspace* を返す
```

## 依存関係

- YAML-cpp
- `Instance` 継承ツリー全体

## 使われる場所

- `SceneRuntime` の隔離Stage、および単体のシーン読込処理から呼ばれる

## 保存ヘッダーと移行状態

SurfaceMarkのFilterInstancesはWorkspace相対パス配列として保存し、全tree構築後に解決する。未解決パスは警告して保持し、ライブ参照は保存直前にrename/move後のパスへ更新する。

Scene YAMLには`recubin: {type: scene, version: 0}`を付けられる。`version: 1`以上や
`type`がscene以外の文書は、将来形式として明示的に拒否する。旧ファイル（ヘッダーなし）は
暗黙のversion 0として読み込む。新規保存では移行済みの場合だけ
`migrations.character_animation_bindings.version: 1`を出力する。

廃止済みの`migrations.default_r6_animations`と`animations.r6_walk.ContentPath`は旧Scene移行用の
read-only互換データとして読み取るが、新規保存へは出力しない。HumanoidのWalk/Jump/Equip参照は
通常のInstanceプロパティとして保存し、全Tree構築後にAnimation Instanceへ解決する。
`serializeSceneResult`と正式保存は
同じEmitter経路を利用するため、Autosaveも正式Sceneと同一形式になる。

### シーンのアセット基準ディレクトリ

`recubin.assetRoot` が存在する場合、その値はシーンファイルの親ディレクトリからの
相対ディレクトリとして扱う。`Texture`、Materialの画像マップ、Skyboxなど、ロード中に
生成される外部ファイル参照は、SceneLoaderのロード期間中この基準を一時的に使用する。
`assetRoot` が空または存在しない旧形式のシーンは、従来どおりプロセスのカレント
ディレクトリを基準とする。絶対パスの`assetRoot`は共有性を損なうため拒否する。

SceneRuntimeのstageではロード失敗時に以前のアセット基準へ戻し、commitが完了した場合だけ
新しい基準を有効にする。実行時の外部ファイルアクセス制限（AssetGuard等）はこの基準を
緩和しない。

`SerializeResult`はnull root、Emitter失敗、シリアライズ例外時に説明メッセージを返す。
`saveSceneResult`はこの結果を利用し、成功／失敗だけを必要とする既存呼び出し向けのbool APIを維持する。

### Scriptソースの保存

`Script`、`LocalScript`、`ModuleScript`の外部参照は従来どおり`Properties.ContentPath`と
`ScriptExtension`で保存する。埋め込みScriptは`Embedded: true`と専用の`EmbeddedSource`キーで本文を保存し、
`ContentPath`は空にする。旧`Properties.Source`はPathの別名として扱われるため、埋め込み本文には流用しない。
一時作業ファイルのパスはSceneへ出力しない。

### Save As とアセット基準

EditorのSave AsはSceneLoaderのシリアライズ結果から依存を収集する。コピーを選んだ場合は
`SceneAssetCopy`が旧基準から新しいシーン親へ依存をコピーし、保存する参照を新基準からの
相対パスへ置換する。旧基準外の依存は新基準の`external/`配下へ置く。既存ファイルはバイト一致時
だけ再利用し、異なる内容の同名ファイルは上書きしない。欠落依存、衝突、I/O失敗ではSceneの保存を
完了せず、EditorManagerが参照、metadata、scene pathを元へ戻す。コピーを拒否した場合は旧基準を
保持した相対`assetRoot`を新Sceneへ記録する。
