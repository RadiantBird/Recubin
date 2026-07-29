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
