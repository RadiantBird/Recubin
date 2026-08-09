## Todoリスト
(!: 中止|?: 不明|~: 保留中|x: 達成済み)

### 設計関係
- CRTPわからん

### エディター関係
- [ ] 同じ名前の画像を外部で編集した後に更新してもキャッシュされてて反映されなくてうざい
  - これは直さなくてもいいんじゃないかなって思ってる。再起動すればいいし
  - 余裕出たら直せばOK

- [ ] 特定の名前パターンのときに無限名前変更警告ループができるので、直す

- [ ] BaseCubeにLockedプロパティを追加し、ビューポートから選択することをできなくする
  - [ ] Luauバインディングもしとく(必要性は不明、できたほうがよさそう)
  - [ ] 実装したけど選択ボックスに変な経路でLockedのCubeが入ってしまうが、原因が不明
  
- [ ] 選択モードに表面のドラッグ、移動モードと同じものを実装する
- [ ] ビューポート外のマウスのスクロールは、カメラのピンチイン、ピンチアウトに反映するべきではないので、これを修正する

# 物理エンジン関係

- N/A
 
### レンダリング関連

- [~] BaseCubeマテリアルに基づくPBRレンダリング(保留)
  - 反射率プロパティを新しく追加
  - レイトレーシングはしない
  - Q. これはOpenGL-4.1で可能？

### ネットワーク関係

- [ ] IPv4 NAT越え（STUN＋ランデブー＋UDPホールパンチ）を完了する
    - [x] ルームコード作成/参加、候補交換、admission token検証、ENet接続、同一ソケットでのホスト移行を実装
    - [x] ローカルのC++ NAT codec回帰テストとPythonランデブー統合テストを実装
    - [ ] 公開配置したSTUN/ランデブーを使い、異なる2回線で接続を実地検証
    - [ ] NAT越しの3ピアでホスト移行とPeerId維持を実地検証
      - 3つも端末がないので、Windowsでローカルホスト経由で2ピア、NAT越しのMac1台でテスト
    - TURN/リレーとIPv6は今回の対象外。上記2件の外部検証が済むまで完了扱いにしない
    - [x] Tailscale接続が成功
    - [ ]「direct接続失敗後も Connecting のまま」という診断不備の修正

- [?] クライアントからリモートアバターが見えない(報告)→現ビルド+実シーンのヘッドレス検証では生成・姿勢追従とも正常を確認。旧ビルド起因の可能性大、新パッケージで要再確認。なお全キャラがスポーン時に上のPlateをすり抜けて地下のPlate1(Y=-45)に着地している(テンプレートRootが床にめり込んでおりPhysX押し出しが下向きに解決されるため。シーン側の配置起因)

- [?] 全workspaceを監視し、オブジェクトの生成/削除を同期する

- [~] NetworkEvent

- [ ] エディターでもネットワーク環境でのテストをできるようにする(ローカルホスト、クライアントと仮想サーバー)
  - 要するにテストのコンテキストがクライアント側できちんとできるので、パッケージしたあとにバグがあってイライラすることが少なくなる

### その他

- カメラに加速度を実装しよう
  - デルタタイムが1.5秒を超えたら加速。加速度はまあ適当にやる
- glb解析がMacだと失敗する模様。ファイルパスの問題か？でもファイルは存在するし
  - スクリプトにも同様の問題あり

### 最重要

- 失敗した
```

Redbird NetworkTest % ./RecubinEngine-macos-arm64 --direct-connect 100.90.255.77:41001 --listen-port 41003
[Chat] Type a message and press Enter (maximum 512 UTF-8 bytes).
[RCBN_DEBUG][AssetGuard.cpp:29] AssetGuard enabled. Root: /Users/Redbird/Recubin/TestCases/NetworkTest
[RCBN_WARN][LuauEngine.cpp:299] PropertyRegistry class 'Highlight' は registerClass 済みですが applyToDispatch されていません（Luau から不可視）。InitDispatchTable_* に applyToDispatch を追加してください。
[RCBN_WARN][Renderer.cpp:2122] Failed to load texture: assets\image\hooo.png
[RCBN_WARN][MeshCube.cpp:370] MeshCube: GLBの解析に失敗しました: assets\models\hair-Curuata.glb
[RCBN_WARN][MeshCube.cpp:370] MeshCube: GLBの解析に失敗しました: assets\models\GirlTorso.glb
[RCBN_WARN][MeshCube.cpp:370] MeshCube: GLBの解析に失敗しました: assets\models\newHead.glb
[RCBN_WARN][Renderer.cpp:2122] Failed to load texture: assets\image\hooo.png
[FileLoader] Error: Could not open binary file: assets\scripts\Prox.luauc
[RCBN_WARN][Script.cpp:33] Failed to load bytecode: assets\scripts\Prox.luauc
[RCBN_WARN][Sound.cpp:115] Failed to load audio:
[FileLoader] Error: Could not open binary file: assets\scripts\FallingSafe.luauc
[RCBN_WARN][Script.cpp:33] Failed to load bytecode: assets\scripts\FallingSafe.luauc
[RCBN_DEBUG][NetworkManager.cpp:207] NetworkManager: local CPU score = 382231
[RCBN_DEBUG][NetworkManager.cpp:213] NetworkManager: connecting to 100.90.255.77:41001 ...
hello???
I'm blind T[RCBN_DEBUG][NetworkManager.cpp:1158] NetworkManager: peer disconnected
^t
why
失敗してしまいました。どうすれば接続できるようになりますか
```

------------------------------------------------------------------------
## Todo++リスト -v2.0-

- p2pネットワーク
- Programインスタンス(Recubinヘッダーで動かすexeをIPC)
- midiプレーヤー
- FrutigerAeroなエディター
  - まずアンチエイリアシング
- パラレルLuau
- Luar言語本気出す
- ネットワークによるエディターの共同作業の実現

------------------------------------------------------------------------
