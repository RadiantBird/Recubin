# Luau バインディング 未実装チェックリスト

> 目的: Instance 各クラスの public なプロパティ / メソッド / シグナルのうち、**Luau から到達できないもの**を洗い出す。
> このドキュメントは Luau 配線の監査結果。実装済み項目は実装側の schema/dispatch と同期して更新する。
>
> 対象範囲: プロパティ + メソッド + 未実装 Luau 型（CFrame / Quaternion 等）。

## 凡例

- ✅ … バインド済み（Luau から読み書き or 呼び出し可能）
- ❌ … 未バインド
- ⚠️ … 部分的（getのみ / setのみ 等）
- 🔧 **要配線** … `PropertyRegistry::registerClass` 済みだが `applyToDispatch` 漏れで Luau 未到達（バグ）
- 経路: `PR`=PropertyRegistry経由 / `手書き`=Dispatch直書き / `新規`=どこにも無い

## 監査の根拠（再現手順）

1. 各 `include/Instances/*.hpp` の public メンバを列挙。
2. 各 `src/Instances/*.cpp` の `registerClass(...)` 静的初期化子の内容と照合。
3. `src/Core/LuauEngine_Dispatch.cpp` の `DispatchTable` / `SetterTable` 直書き、および
   `applyToDispatch("...")` 呼び出しリストと照合。
4. 差分（C++に存在するが Luau 未到達）を ❌ として抽出。

**監査時点の確定事実:**
- Physics/Character 系の `Force`, `Gyro`, `Motor6D` は schema 登録と Luau dispatch 配線済み。
- `PhysicsConstraint` と派生の `Rope`, `Rod`, `BallSocket`, `NoCollision`, `Weld`, `Motor` も
  schema を基準に YAML/clone/editor/Luau を接続済み。
- ImageLabel / ImageButton の dispatch 配線漏れなど、今回の対象外の既存項目は下記の未実装表に残す。

---

## 0. 最優先（バグ・基盤の欠落）

> パス系の既存規約は維持し、今回対象の参照プロパティは型付き Instance picker と既存の相対パス解決を共有する。
- [ ] 🔧 **ImageLabel.Image** — `registerClass` 済みだが `InitDispatchTable_GUI()` の applyToDispatch に無く Luau から不可視。`PropertyRegistry::applyToDispatch("ImageLabel", ...)` を1行追加するだけで解消。
- [ ] 🔧 **ImageButton.Image** — 同上。`applyToDispatch("ImageButton", ...)` 追加。
- [x] ❌ **CFrame 型（Luau）** — メタテーブル未定義（現状は `RCBN_Vector3/Vector2/Color4` のみ）。`Spatial.cframe` を公開する前提となる基盤。
- [x] ❌ **Quaternion 型（Luau）** — 同上。`Rotation` 公開の前提。
- [x] ❌ **Spatial 基底のバインド** — `Position/Size/Rotation/cframe` を Spatial レベルで未公開。`BaseCube` は Position/Size のみ個別手書き。そのため **Spatial を直接継承する Model / Sound の Position・Size が読み書き不可**。
- [x] ❌ **Rotation（全 Spatial 系）** — BaseCube 含め `Rotation`(Quaternion) / `cframe`(CFrame) がどのクラスでも未バインド。回転を Luau から扱う手段が存在しない（Quaternion/CFrame 型実装が前提）。

---

## 1. Instance（基底）

| メンバ | 種別 | 状態 | 経路 | 備考 |
|---|---|---|---|---|
| Name | Field(string) | ✅ get/set | 手書き | setter は親 children マップを整合 |
| Parent | Field | ⚠️ get のみ | 手書き | **set 未実装**（reparent を Luau からできない） |
| FindChild / GetChildren / WaitChild / IsA / Destroy | Method | ✅ | 手書き | |
| clone / getChildByPath / getFullPath | Method | ❌ | 新規 | clone は Roblox の `:Clone()` 相当。要検討 |
> reparentはできたほうがいいと思います。Parentのセッターを実装しましょう。
> cloneも必要です。
---

## 2. Spatial 系（Spatial / BaseCube とその派生）

### Spatial（基底）
| メンバ | 種別 | 状態 | 備考 |
|---|---|---|---|
| Position | Field(Vec3) | ⚠️ | BaseCube のみ手書き。Spatial 直系(Model/Sound)は ❌ |
| Size | Field(Vec3) | ⚠️ | 同上 |
| Rotation | Field(Quaternion) | ❌ | Quaternion 型が前提 |
| cframe | Field(CFrame) | ❌ | CFrame 型が前提 |
| getWorldCFrame / getWorldPosition | Method | ❌ | ワールド座標取得。Luau から有用 |

### BaseCube
| メンバ | 種別 | 状態 | 経路 | 備考 |
|---|---|---|---|---|
| Position / Size / Color / Anchored / CanCollide / CanTouch | Field | ✅ | PropertyRegistry | Position は Box3D pose 同期、Size は setSize 経由 |
| Velocity | 派生(read) | ✅ get | 手書き | actor から読む。set は無し（仕様妥当） |
| Touched / TouchEnded | Signal | ✅ | 手書き | Box3Dの形状overlap開始/終了。CanCollideとは独立 |
| **CastShadow** | Field(bool) | ❌ | 新規 | |
| **Unlit** | Field(bool) | ❌ | 新規 | |
| **UseTriplanar** | Field(bool) | ❌ | 新規 | |
| **TextureScale** | Field(float) | ❌ | 新規 | |
| **material / Material** | Field(Material) | ❌ | 新規 | Material 型の Luau 表現要検討（MaterialType enum 等） |
| **Rotation** | Field(Quaternion) | ❌ | 新規 | Quaternion 型前提 |
| teleportTo / setRotation / syncPhysics | Method | ❌ | 新規 | setSize/setAnchored は setter 経由で実質公開済 |

### Cube / Cylinder / Sphere / TriangularPrism / Skybox
| クラス | 固有メンバ | 状態 | 備考 |
|---|---|---|---|
| Cube / Cylinder / Sphere / TriangularPrism | 固有プロパティ無し | ✅ | BaseCube 継承分のみ。追加不要 |
| **Skybox** | skyboxPaths[6] / setSkyboxPath | ❌ | 6面テクスチャパス未公開 |
| Sun | Angle | ✅ | applyToDispatch 済 |
| Moon | （無し） | ✅ | |
| **LiquidCube** | Density | ✅ | applyToDispatch 済 |
| **MeshCube** | MeshFile | ✅ | PropertyRegistry経由。setter は loadFromGLB と物理actor再生成を呼ぶ |

---

## 3. ライト系

| クラス | メンバ | 状態 | 備考 |
|---|---|---|---|
| Lighting | Direction / Brightness / Color | ✅ | applyToDispatch 済 |
| LightSource | Color / Brightness / Range | ✅ | applyToDispatch 済 |
| PointLight | （LightSource 継承のみ） | ✅ | |
| SpotLight | Angle | ✅ | applyToDispatch 済 |

---

## 4. GUI 系

| クラス | メンバ | 状態 | 備考 |
|---|---|---|---|
| ScreenGuiObject | Position/Size/Norm/Visible/Active/ZIndex/BackgroundColor/Transparency/Hovered | ✅ | applyToDispatch 済 |
| GuiButton | Activated(Signal) | ✅ | |
| TextLabel / TextButton | Text / TextColor / FontSize | ✅ | |
| WorldGuiObject | Size/Norm/Active/Visible/BackgroundColor/ZIndex/Transparency | ✅ | |
| SurfaceGui | Face | ✅ | |
| BillboardGui | Mode/SizeMode/Offset | ✅ | SizeModeはScreen/World文字列、OffsetはVector3 |
| ProximityPrompt | KeyboardKeyCode/HoldDuration/MaxActivationDistance/Enabled/ActionText/ObjectText/Triggered | ✅ | |
| **ImageLabel** | Image | 🔧 | **要配線**（最優先セクション参照） |
| **ImageButton** | Image | 🔧 | **要配線**（最優先セクション参照） |

---

## 5. 物理コンストレイント（Weld / Rope / Rod / Motor）

共通の Enabled と各クラス固有プロパティを PropertyRegistry から配線する。

| クラス | バインド済 | 未バインド |
|---|---|---|
| Weld | Cube0 / Cube1 / Enabled | — |
| Rope | MaxDistance / Stiffness / Damping / LineWidth / Color / Cube0 / Cube1 / Attachment0 / Attachment1 / Enabled | — |
| Rod | LineWidth / Color / Cube0 / Cube1 / Attachment0 / Attachment1 / Enabled | — |
| BallSocket | Cube0 / Cube1 / Attachment0 / Attachment1 / Enabled | — |
| NoCollision | Cube0 / Cube1 / Enabled | — |
| Motor | Axis / DriveVelocity / MaxForce / Cube0 / Cube1 / Attachment0 / Attachment1 / Enabled | — |

---

## 6. その他クラス

| クラス | メンバ | 状態 | 備考 |
|---|---|---|---|
| Workspace | Gravity / PhysicsEnabled / Raycast | ✅ | 他に公開対象の public 無し |
| Sound | IsPlaying/Looped/Volume/Speed/PreservePitch/TimePosition/Length/Play/Stop/Reset/Seek | ✅ | |
| Sound | **autoPlay** | ❌ | getAutoPlay/autoPlay 未公開 |
| Sound | **ContentPath / SoundGroup** | ❌ | getContentPath/getSoundGroup 未公開（read-only で有用） |
| Sound | Position / Size | ❌ | Spatial 基底未バインド（上記参照） |
| Humanoid | WalkSpeed/JumpPower/MaxHealth/RespawnTime/Health/Died/TakeDamage/Play・Pause・StopAnimation | ✅ | |
| Humanoid | **isDead / getRootWorldPosition / getHeadWorldPosition** | ❌ | 公開検討 |
| Script | Enabled(get/set) / Path(get/set) / Source(get) | ⚠️ | **Source は set 未実装** |
| **Decal** | TextureID / Face | ✅ | |
| **Decal** | **texturePath / Color** | ❌ | 2プロパティ未公開 |
| **Texture** | TextureID/Face/texturePath/Color/StudsPerTileU/StudsPerTileV | ❌ | **クラスごと未バインド**（Dispatch に "Texture" 無し） |
| **PostEffect** | Enabled/Type/ZIndex/Intensity/Param1/Param2 | ❌ | **クラスごと未バインド** |
| **Animation** | Length / Speed / addOrReplaceKey / removeKey / import・exportToFile | ❌ | **クラスごと未バインド** |
| **Tool** | Activated(Signal) | ✅ | |
| **Tool** | Equipped / Hand / Handle | ✅ | Equipped は Luau 読取専用。Hand/Handle は読み書き可能 |
| **Model** | Position / Size | ❌ | Spatial 基底未バインド |
| System | Heartbeat | ✅ | |
| System | BaseResolution | ⚠️ get のみ | PR経由・`.luaReadOnly()`で意図的にset不可（ScreenGui基準解像度） |
| Event | Fire | ✅ | |
| UserInput | Pressed / Released / IsPressed | ✅ | |
| User | Input / AddTool / RemoveTool / GetTool / GetTools | ✅ | |
| AppImage | IconPath | ✅ | |
| Terrain | Enabled/DataPath/Seed/Flat/SetBlock/RemoveBlock/GetBlock/Raycast/ApplyBrush | ✅ | |
| Folder / StarterCharacter | （固有プロパティ無し） | ✅ | コンテナ。追加不要 |

---

## 集計（未実装の主な塊）

1. **基盤型 2件**: CFrame, Quaternion（Luau 型そのものが未実装）
2. **要配線バグ 2件**: ImageLabel.Image, ImageButton.Image
3. **Spatial 基底**: Position/Size/Rotation/cframe（Model/Sound に波及）
4. **クラスごと未バインド 3件**: Texture, PostEffect, Animation
5. **個別プロパティ漏れ**: BaseCube(CastShadow/Unlit/UseTriplanar/TextureScale/material/Rotation),
   Decal(texturePath/Color), Sound(autoPlay/ContentPath/SoundGroup),
   Skybox(skyboxPaths), Script(Source setter), Instance(Parent setter)

> 注: §6 のうち Animation / MeshCube / Skybox / Model / Folder / StarterCharacter / UserInput / System / Event
> のメンバ詳細は調査時のヘッダ要約に基づく。実装着手前に該当ヘッダを再確認すること。
