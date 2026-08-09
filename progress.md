# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---

## 2026-07-29: IPv4 NAT越え実装・ローカル検証

- STUN Binding、8文字ルームコードのランデブー、Local/ServerReflexive/PeerReflexive候補交換、
  双方向UDPホールパンチから同一ソケット上のENet接続までを実装。
- 128-bit admission token、protocol version、room epochでHelloを検証し、候補更新と既存ソケットを
  用いたHost Promote/再パンチをホスト移行経路へ統合。
- Python標準ライブラリのみのIPv4 UDPランデブーサービスと、C++ NAT codec回帰テスト、
  Python統合テストを追加。ローカル3ピアでルーム接続、PeerId 2のUDP 41002据え置き昇格、
  PeerId 3のUDP 41003据え置き再接続を確認。移行時の旧Host TTL競合も再現テストを加えて修正。
- 未完了: 公開STUN/ランデブーを用いた異なる2回線での接続、およびNAT越し3ピアのホスト移行実地検証。
- macOSでPhysXや描画を起動せず同じ`NetworkManager`を検証できる独立CLI
  `tools/network_probe`を追加。別回線での候補、チャット、Roster、PeerId/UDP port維持を表示する。

## 2026-08-03: ネットワークテストCI/CD

- pushとPull RequestでPythonランデブーテストを実行し、Windows/macOSのRelease版
  `RecubinNetworkProbe`をビルドするGitHub Actionsを追加。
- direct Host/Clientの2ピアloopback疎通、双方の`READY expected-peers=2`と双方向チャットを
  標準ライブラリのみで検証し、OS別zipをartifactとして保存するようにした。
- `v*`タグではテスト成功後にGitHub Releaseを作成または再利用し、OS別zipを更新する。

## 2026-08-09: Viewport QoL改善

- LockedをLuau Read/Writeへ公開し、最前面Lockedの遮蔽・クリック解除・矩形選択開始を維持した。
- 通常クリックを最前面Cubeから最上位Modelへ昇格し、Select／Move共通の表面ドラッグとModel子孫AABBによる衝突フィットを追加した。
- Editor選択表示を塗りなしの実形状外枠へ統一し、旧画面矩形／Model AABB線を廃止。ギズモモードのクリック候補へ白い事前外枠を追加した。
- Resizeを初期サイズ非依存のワールド単位加算式へ変更し、`User::gizmoSize`（既定0.20）をEditor設定として保存するようにした。
- Primaryカメラのキーボードズームとホイール許可を分離し、Viewport外スクロールを消費のみとしてカメラへ適用しないようにした。
- Viewportヘルパー回帰を34項目へ拡張し、フルビルドと全項目の成功を確認した。
