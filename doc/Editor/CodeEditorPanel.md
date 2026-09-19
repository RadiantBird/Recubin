# CodeEditorPanel

`CodeEditorPanel` は、Explorer から開いた `Script` / `TextFile` を中央 Dock のタブで編集する補助エディタです。

- `SceneHierarchyPanel::onOpenEditor` がダブルクリックを `EditorManager` へ通知する。
- `EditorManager` は対象 `Instance` ごとに一つのパネルを所有し、`MainDockSpace` へドッキングする。
- 本文と行番号は同じ `InputTextMultiline` のスクロール領域を使うため、縦スクロールが同期する。横スクロールは本文だけが移動する。
- 本文と行番号には起動時に FontAtlas へ一度だけ追加した `assets/fonts/JetBrainsMono-Medium.ttf` を使う。通常の UI フォントは変更しない。
- Script は Luau の軽量 lexer（キーワード、型、文字列、数値、コメント、演算子）で色分けする。`--[[...]]` と `[[...]]` は行をまたいで状態を保持する。
- Ctrl+S は Script の `Source` と元の `Path`、または TextFile の RuntimeFileSystem overlay (`StorageId`) へ反映する。コードファイルの保存では EditorManager の Scene dirty は変更しない。
- タブを閉じるときに dirty なら Save / Discard / Cancel を表示する。Recubin Studioの終了時にもコード用の未保存確認を表示し、Save / Quit Without Saving / Cancel を選択できる。対象 Instance が削除された場合は weak pointer を解放してタブを閉じる。

LSP、補完、診断、折りたたみ、ミニマップなどの IDE 機能は持たない。
