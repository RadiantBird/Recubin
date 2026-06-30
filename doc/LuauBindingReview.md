# コードレビュー: Luau バインディング層（改善案）

> 対象: `LuauEngine` / `LuauEngine_Dispatch` / `PropertyRegistry` / `RCBNScriptSignal` 周辺。
> 性質: 改善提案。実装はまだ行っていない（採否は別途判断）。

---

## 重大度: 高

### H-1. デバッグ出力が本番経路に残存
**箇所:** `src/Core/LuauEngine_Dispatch.cpp:296`

```cpp
DispatchTable["Instance"]["Name"] = [](lua_State* L, Instance* obj) {
    std::cout << "Accessing Name of Instance: " << obj->Name << std::endl;  // ← これ
    lua_pushstring(L, obj->Name.c_str());
    return 1;
};
```

- `instance.Name` を読むたびに stdout へ出力される。スクリプトが毎フレーム Name を参照すると
  ログが溢れ、`std::endl` の flush で体感できる性能劣化も起こり得る。
- **提案:** 行を削除（または `Logger` のデバッグレベル経由に変更）。他の getter には無いので単純削除で良い。
> わかりました、修正しましょう。

### H-2. `applyToDispatch` 呼び忘れによる「サイレント未公開」（ImageLabel/ImageButton）
**箇所:** `src/Core/LuauEngine_Dispatch.cpp::InitDispatchTable_GUI()` (541-548)

- `registerClass` と `applyToDispatch` が分離しているため、登録だけして配線を忘れると
  **コンパイルも通り YAML も動くのに Luau からだけ見えない**という発見しづらいバグになる。
  現に ImageLabel / ImageButton がこの状態。
- **提案（恒久対策）:** `PropertyRegistry` に登録済み全クラス名を列挙する API
  （例 `registeredClassNames()`）を追加し、`InitDispatchTable_*` の最後で
  「未 apply のクラスを自動 apply する / もしくは未 apply を起動時に警告ログ」する。
  これにより手書きの apply リスト保守が不要になり、同種の漏れを構造的に防げる。
- **暫定対策:** GUI セクションに2行追加（`applyToDispatch("ImageLabel"/"ImageButton")`）。
> これも承認します。
---

## 重大度: 中

### M-1. `instance_index` / `instance_newindex` の線形走査
**箇所:** `src/Core/LuauEngine.cpp`（`instance_index`, `instance_newindex`）

```cpp
for (const auto& [className, classProps] : DispatchTable) {
    if (obj->IsA(std::string(className))) { ... }
}
```

- プロパティアクセス1回ごとに **DispatchTable 全クラス**を走査し、各々に対し
  `IsA(std::string(...))`（`std::string` の一時生成 + 仮想呼び出し + 文字列比較）を行う。
  クラス数 × アクセス頻度で O(N) のコストが毎回かかる。
- さらに `IsA` の評価順がマップのハッシュ順依存のため、派生クラス固有プロパティと基底プロパティが
  同名のとき、どちらが当たるか保証されない潜在リスクがある。
- **提案:**
  1. オブジェクトの最派生クラス名（`getClassName()`）をキーに、継承チェーンを一度だけ平坦化した
     `className → {prop → getter}` のマージ済みテーブルを構築・キャッシュする
     （`collectSchema` が既に継承解決を持つので流用できる）。
  2. アクセス時は `getClassName()` で直接ルックアップ → O(1)。`IsA` ループを廃止。
  3. 文字列キーは `std::string_view` のまま比較し、`std::string` 一時生成を避ける。
> 採用します。
### M-2. 子フォールバックと property の優先順位が暗黙
**箇所:** `instance_index` のプロパティ探索後の「同名の子を返す」フォールバック

- プロパティに無いキーは子インスタンス名として解決される。これは Roblox 互換で妥当だが、
  **プロパティと同名の子**がいると常にプロパティが優先され、子へはアクセスできない（逆も曖昧）。
- **提案:** 仕様としてドキュメント化し、`spec.md` に「プロパティ優先」を明記。挙動変更は不要。
> 了解しました。
### M-3. 数値 setter に範囲検証が無い
**箇所:** `setter_number` / `setter_method_float` ほか（`LuauEngine_Dispatch.cpp`）

- `PropertyDesc` は `lo/hi/step`（エディタ用レンジ）を持つが、Luau setter は `luaL_checknumber`
  のみでクランプ・検証をしない。エディタ経由では範囲内でも、スクリプトからは範囲外値を書ける。
- **提案:** これは「エディタのみのヒント」という現設計なら**現状維持で問題なし**。
  ただし Health 等の安全性が要る値は `fieldVia`（setter メソッド側でクランプ）に寄せる方針を
  ルール化すると一貫する（Humanoid.Health は既にこの形）。
> そうですね、不正値が入ると困る数値はその形にしましょう。
### M-4. 単一ソース原則からの逸脱（手書き Dispatch の重複保守）
**箇所:** Weld/Rope/Rod/Motor、BaseCube、Sound、Decal 等の手書き Dispatch

- これらは `PropertyRegistry` を使わず Dispatch を直書きしている。直書き自体は副作用の強い
  プロパティ（PhysX 同期等）では妥当だが、**単純フィールド**（Rope.LineWidth, Decal.TextureID 等）まで
  手書きなので、プロパティ追加時に「Dispatch getter / setter / YAML / clone / editor」を
  別々に手当てする必要があり漏れやすい（実際に Decal.Color, Motor.Axis, Texture 全体が未配線）。
- **提案:** 副作用の無い純フィールドは段階的に `PropertyRegistry` 登録 + `applyToDispatch` へ移行し、
  副作用のあるものだけ手書き（または `fieldVia`/`custom`）に残す。CLAUDE.md のメモにある
  「BaseCube 等は特殊なので手書き維持」の線引きを、クラス単位ではなく**プロパティ単位**で
  見直すと重複が減る。

> 注: 当初「Rope/Rod/Motor は PR と Dispatch の二重登録」と疑ったが、grep の結果
> これらは `registerClass` されておらず **Dispatch 手書きのみ**であることを確認済み。
> 二重登録は無く、問題は「純フィールドまで手書きしている」点。

> 手書きする必要性のないものを移行する形で。
---

## 重大度: 低 / 観点メモ

### L-1. Signal のライフサイクルと Lua ref 解放
**箇所:** `RCBNScriptSignal` / `signal_connect_closure`（`lua_ref` で callback を保持）

- `connect` で `lua_ref` した callback の解放経路（`Disconnect` / Script 破棄時 / Workspace 切替時）が
  確実に `lua_unref` まで到達するかを確認したい。welded cube のワークスペース切替で過去に
  ダングリング不具合があった経緯（コミット履歴）を踏まえ、Signal listener も同様の
  「ワークスペース破棄前クリーンアップ」対象になっているかレビュー推奨。
- **アクション:** `RCBNScriptSignal::disconnect` / デストラクタで全 listener の `lua_unref` を
  漏れなく行っているか、`Until`/`Once` の自動解除パスを含めて確認。

### L-2. `weak_ptr` placement new の `__gc` 対称性
**箇所:** Instance userdata の生成（placement new）と `__gc`（明示 `~weak_ptr()`）

- 生成側（`getter_closure` 等で `new (ud) std::weak_ptr<Instance>(...)`）と破棄側 `__gc` は
  対になっているが、**生成箇所が複数に分散**している。新しい userdata 生成ヘルパを増やす際に
  メタテーブル設定を忘れると `__gc` が呼ばれずリークする。
- **提案:** `pushInstance(L, shared_ptr)` のような単一ヘルパに集約し、placement new +
  メタテーブル設定を1箇所に閉じ込める（既に部分的に重複コードがある）。

### L-3. enum 文字列変換の重複
**箇所:** `normToStr/strToNorm`, `faceToStr/strToFace`（Dispatch）と `enumProp` のテーブル

- Norm/Face の文字列対応が Dispatch ヘルパと `enumProp` の両方に存在し得る。`enumProp` の
  `enumNames` を単一の真実として、Dispatch 側ヘルパもそれを参照する形に寄せると二重定義を防げる。

### L-4. `std::cout` 直接使用 vs Logger
- H-1 以外にも Dispatch/Engine 内で `std::cout` を使う箇所があれば、プロジェクトの `Logger`
  経由に統一するとコンソールパネルへの集約・レベル制御ができる。

> 一応確認しましょう。
---

## まとめ（推奨着手順）

1. **即修正:** H-1（cout 削除）、H-2 暫定（ImageLabel/ImageButton の apply 2行）。
2. **基盤:** M-1（dispatch をクラス名直引き + キャッシュ化）。アクセス毎の走査を解消。
3. **恒久化:** H-2 恒久（registerClass 済みの自動 apply / 未 apply 警告）。
4. **整理:** M-4（純フィールドの PR 移行）、L-2（userdata 生成ヘルパ集約）。
5. **確認:** L-1（Signal の lua_unref 漏れ点検）。
