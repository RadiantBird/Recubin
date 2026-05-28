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