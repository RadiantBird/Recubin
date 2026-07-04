# Weather

`include/Instances/Weather.hpp`

天気システムを統合するクラス。雲・雨・雪・風・雷・環境音を1つのインスタンスで扱う。
Workspaceの直接の子として配置する想定（Sun/Moon/Skyboxと同じ探索方針。`Lighting`のような
再帰探索はしない）。自身は`Spatial`ではない（`Lighting`/`System`と同型）。

## 継承

`Instance` → `Weather`

## メンバ変数

| 変数 | 型 | 説明 |
|---|---|---|
| `Enabled` | `bool` | falseで雨/雪/雷/雲描画すべて停止 |
| `CurrentWeather` | `WeatherType`(enum) | `Clear`/`Rain`/`Snow`。Luauから読み書き可能 |
| `CloudCover` | `float` | 雲の被覆率 [0,1] |
| `CloudDensity` | `float` | 雲の濃さ・不透明度 [0,1] |
| `CloudHeight` | `float` | カメラ基準の雲層の高さ(stud) |
| `WindDirection` | `Vector3` | 風向き+速さ。`Workspace::Wind`へ毎フレーム反映される |
| `LightningEnabled` | `bool` | 雷の個別無効化スイッチ |
| `LightningChance` | `float` | 1秒あたりの落雷発生確率係数 |
| `ClearAmbientPath`/`RainAmbientPath`/`SnowAmbientPath` | `string` | 天候ごとの環境音ファイルパス（差し替え可能） |
| `AmbientVolume` | `float` | 環境音の音量 [0,1] |

## 内部状態（非公開・非シリアライズ）

`update()`初回呼び出し時に`ensureChildren()`が遅延生成する子オブジェクト群。コンストラクタでは
生成しない（`Instance::addChild()`は内部で`shared_from_this()`を呼ぶため、コンストラクタ内で
`this`に対し呼ぶと`std::bad_weak_ptr`になる）。

| 内部メンバ | 型 | 役割 |
|---|---|---|
| `m_skyAnchor` | `Cube`(不可視) | カメラ追従。雨/雪`ParticleEmitter`の親 |
| `m_lightningAnchor` | `Cube`(不可視) | 落雷時のみ再配置。落雷ライト/スパークの親 |
| `m_rainEmitter`/`m_snowEmitter` | `ParticleEmitter` | `CurrentWeather`に応じて`Enabled`を切替 |
| `m_lightningLight` | `PointLight` | 落雷時に`brightness`をスパイクさせ、2乗カーブで減衰 |
| `m_lightningSparks` | `ParticleEmitter`(`Enabled=false`) | 落雷時に`emit(count)`でバースト |
| `m_ambientSound` | `Sound`(Weather直下=非Spatial親→グローバル再生) | 天候ごとの環境音 |

これらは本物のツリー内インスタンスなので、既存の`ParticleEmitter::updateAll`/`collectParticleEmitters`/
`collectLights`が**Renderer側の変更なしに自動的に**拾って描画・更新する。

## メソッド

| メソッド | 説明 |
|---|---|
| `update(dt, cameraPosition)` | 毎フレーム1回だけ呼ぶ。`ensureChildren()`→アンカー追従→雨/雪切替→
  `Workspace::Wind`書込→雲UVスクロール→環境音切替→雷判定→フラッシュ減衰 |
| `getCloudScrollOffset()` | Rendererが読む雲UVスクロール量（読み取り専用） |
| `updateAll(workspaceRoot, dt, cameraPosition)` *(static)* | Workspace直下から`Weather`を探して`update()`する（再帰ではなく直接の子のみ） |
| `setProperty(name, value)` | YAML デシリアライズ用 |
| `clone()` | トップレベルのプロパティのみコピーし、子は複製しない（`ensureChildren()`が再構築するため） |

## シミュレーションと描画の分離

`ParticleEmitter`と同様、`update()`はメインループから毎フレーム1回だけ呼ぶ（`renderViewport`は
ビューポートの数だけ複数回呼ばれるため、そこで状態を進めると多重更新になる）。`Renderer::renderClouds()`は
`Weather`の`CloudCover`/`CloudDensity`/`CloudHeight`/`getCloudScrollOffset()`を読むだけで、状態を変更しない。

## 雷の仕組み

`CurrentWeather==Rain`かつ`LightningEnabled`のときのみ、`LightningChance*dt`の確率で`attemptStrike()`を試みる。
ワークスペース内の`MaterialType::Metal`な`BaseCube`派生を再帰収集し、`(高さ+1)^2`で重み付けした乱数選択で
対象を決定、`Physics::raycast`で空が開けているか（間に遮蔽物がないか）確認してから発火する。ダメージ・延焼は
実装しない（視覚効果のみ）。

## 既知の制約

- 子（雨/雪/雷/環境音インスタンス）はシリアライズされない。手動で調整した色・速度等は保存/複製で失われ、
  再読込・複製のたびにハードコードされた既定値で再構築される。
- 雨/雪の`CollisionCutoff`はPhysXが初期化済み（Playを一度は押した）シーンでのみ有効（既存のPhysXライフサイクルと同じ制約）。
- 1つのWorkspaceに複数の`Weather`を置いた場合の挙動は未定義（Sun/Moon/Skyboxと同じ既存の曖昧さ）。

## 依存関係

- `Instance`, `Workspace`（`Wind`/`getPhysicsEngine()`）, `Physics`（`raycast`）
- `Cube`, `ParticleEmitter`, `PointLight`, `Sound`, `AudioService`
- `Vector2`/`Vector3`, `Color4`, `Util/Material`（`MaterialType::Metal`）

## 使われる場所

- `SceneHierarchyPanel::renderInsertMenu()`「その他」サブメニューから配置
- `PropertiesPanel`のスキーマ駆動インスペクタ＋環境音3種の参照ボタンでプロパティ編集
- `Renderer::renderClouds()`が雲の描画に、`ParticleEmitter`/`PointLight`の既存機構が雨/雪/雷の描画に使用
- `LuauEngine`のバインディングで全プロパティの読み書きが可能
