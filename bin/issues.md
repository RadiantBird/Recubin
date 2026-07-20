# 既知のバグ

## 1
停止ボタンを押し、"Switched to Free Camera mode."が表示されたが、カメラ操作がCharacterのままでロックされている。
```cpp
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
if (ImGui::Button("  Stop  ", btnSz)) {
    mode = EditorMode::Edit;
    if (m_user) {
        m_user->controlMode = User::ControlMode::Free;
        RCBN_LOG("[INFO] Stopped. Switched to Free Camera mode.");
    }
    else {
        RCBN_LOG("[???] User instance is null.");
    }
}
```

調査からすると、カメラモードはフリーモードである。
よって、ビューポートがテストプレイを終了した後にフォーカスが混乱し、カメラ操作を受け付けない状態になっていると考えられる。
--> 原因は物理エンジンの未初期化で入力処理がブロックされていたことだった。

## 2
複数ワークスペースのシステムが中途半端に実装されており、Workspaceがシングルトンとmainでは想定されているが、これは意図通りではない。
そのため、現時点では不適切なキー衝突が起きたりして、致命的である。
このエラーは、Workspaceを追加し、プレイヤーを転送したときに発生した。
```
例外が発生しました: W32/0xC0000005
Unhandled exception at 0x0000000000000000 in Recubin.exe: 0xC0000005: Access violation executing location 0x0000000000000000.
```
そのため、デストラクタで処理が破綻していることが確定している。

解決方針: Workspaceを配列化
-->解決済み

## 3
Workspaceを新しいビューポートで開くが、しかしながらショートカットキーやギズモがすべて使えない状態になっている。
メインのGUIから移植する必要あり。もしくはテンプレートかクラス化。そっちのほうが良いかも。

## 4 Terrain関係に問題あり
Terrainの読み込みがカメラ位置に依存している。User->Characterを基準にするべき。
terrainディレクトリが未使用。そのため、毎回のようにチャンクが生成され、保存されることがない。