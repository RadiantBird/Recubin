# IPv4 NAT越え

Recubinのネットワークランタイムは、STUNで外向きUDP候補を取得し、軽量ランデブーサービスで
ルーム参加者の候補を交換した後、双方向UDPホールパンチを行う。成功した経路を同じソケット上の
ENet接続へ引き継ぐ。ランデブーサービスは候補交換だけを担い、ゲームパケットは中継しない。

## ゲームの起動

対象シーンの`System.UseNetwork`を`true`にし、ゲームフォルダの`startup.yaml`へ接続先を指定する。
ホスト名だけを指定した場合、STUNは3478、ランデブーは3479を使用する。

```yaml
StunServer: stun.example.net:3478
RendezvousServer: rendezvous.example.net:3479
```

第三者サービスの既定ホスト名はない。両項目が設定されていない、または解決できない場合は
`MissingConfig`で起動に失敗する。CLIの指定は`startup.yaml`より優先される。

```text
RecubinEngine.exe --host
RecubinEngine.exe --host 40000
RecubinEngine.exe --connect 01ARZ3ND
RecubinEngine.exe --connect 01ARZ3ND --listen-port 40001
RecubinEngine.exe --host --stun stun.example.net:3478 --rendezvous rendezvous.example.net:3479
```

- `--host [listen-port]`: ルームを作成し、8文字Crockford Base32コードを標準出力へ表示する。
- `--connect <room-code>`: ルームコードで参加する。接続先IPアドレスは指定しない。
- `--listen-port <port>`: 自ノードのUDPポートを上書きする。省略時または0はOS選択となる。
- `--stun <host[:port]>` / `--rendezvous <host[:port]>`: 接続先を上書きする。

`--host`と`--connect`は同時に指定できない。直接IP接続用のC++ APIは内部互換のため残るが、
ゲームランタイムのCLIからは使用しない。

## 接続フローと失敗

1. 最大32ピアの`ENetHost`をIPv4の任意ローカルポートへ一度だけbindする。
2. Local候補を収集し、同じUDPソケットからRFC 8489 STUN Binding Requestを送る。
3. transaction ID、magic cookie、長さ、IPv4 `XOR-MAPPED-ADDRESS`を検証し、
   ServerReflexive候補を追加する。初回探索は指数バックオフで最大約8秒、接続中は15秒ごとに更新する。
4. ランデブーから送信元IPv4アドレス/ポートに結び付いたHMAC cookieを取得し、CreateまたはJoinする。
5. ランデブーが発行した128-bit admission token、room epoch、Local/ServerReflexive/
   PeerReflexive候補を参加者間で交換する。
6. 全候補へ200ms間隔でProbe/Acknowledgeを送り、受信元をPeerReflexive候補として追加する。
   8秒以内に直接経路が成立すれば、その送信元へ同じソケットからENet接続する。
7. Helloのゲームプロトコルversion、room epoch、admission tokenが一致した場合だけHostがPeerIdを承認する。
   候補とtokenはRosterにも配布される。

STUNとランデブーの再試行には期限があり、無限再接続は行わない。ランタイムは次の失敗分類を
ログと終了コードへ反映する。

- `MissingConfig`: STUN/ランデブー未設定、名前解決失敗、または不正な接続先。
- `StunTimeout`: STUN Bindingが期限内に成功しなかった。
- `RoomNotFound`: ルームが存在しない、失効した、または現在Hostがいない。
- `RoomFull`: 32人の上限に達している。
- `RendezvousTimeout`: ランデブー要求が期限内に完了しない、または一般エラーを返した。
- `PunchTimeout`: 8秒以内に直接UDP経路を確立できない。
- `AdmissionRejected`: tokenや送信元が登録内容と一致しない。
- `EnetTimeout`: bindまたはホールパンチ後のENet handshakeが失敗した。

対称NATなどで直接経路を作れない場合は`PunchTimeout`となる。TURN/ゲーム通信リレーへの
フォールバックは行わない。

## ホスト移行

全ピアはSTUNを15秒ごと、ランデブー登録を5秒ごとに更新し、候補とadmission tokenを保持する。
Host離脱後は従来の決定的な選出規則で新Hostを決める。選出されたClientはUDPソケットを
破棄・再bindせずHostへ昇格し、現在のepochを添えてPromoteを送る。ランデブーは旧Hostの
refreshが10秒以上停止し、epochが一致するとき、最初に届いた認証済み昇格を原子的に受理する。
受理時にepochを進めて候補を再配布するため、同じepochへの後続昇格は拒否される。安全猶予中の
`CONFLICT`は新Hostを停止させず有限再試行し、30秒で登録を打ち切ってキャッシュ候補による接続を継続する。

残存Clientは新Hostへ再度ホールパンチし、Helloで以前のPeerIdを提示する。有効tokenとepochが
一致し、そのPeerIdが現在ほかの接続で使用されていなければ同じPeerIdを維持する。ランデブーが
一時停止している場合もRosterにキャッシュされた候補で直接再接続を試す。

## ランデブーサービス

Python 3の標準ライブラリだけで起動でき、ルーム状態はメモリ上だけに保持する。

```text
python3 tools/rendezvous/server.py --bind 0.0.0.0 --port 3479 \
  --ttl 30 --secret "replace-with-at-least-16-bytes"
```

設定は引数または環境変数で渡せる。

| 設定 | 環境変数 | 既定値 |
| --- | --- | --- |
| bind IPv4アドレス | `RECUBIN_RENDEZVOUS_BIND` | `0.0.0.0` |
| UDPポート | `RECUBIN_RENDEZVOUS_PORT` | `3479` |
| 参加者TTL（秒） | `RECUBIN_RENDEZVOUS_TTL` | `30` |
| HMAC秘密値 | `RECUBIN_RENDEZVOUS_SECRET` | 起動ごとにランダム生成 |

`--secret`は16 bytes以上の平文、または`hex:`に続く16 bytes以上の16進表現を受け付ける。
本番では固定の十分長い秘密値を安全に供給する。サービス再起動時は全ルーム、cookie、tokenが失効する。

各datagramは`RCBN` magic、version 1、message type、payload長、32-bit transaction IDを持つ
network byte orderのバイナリ形式である。Create、Join、CandidateUpdate、Refresh、Promote、
Leave、Errorと、事前のCookieRequest/CookieChallengeを扱う。候補はtype、IPv4 host、portの組で、
1パケットあたり最大32件。datagram上限は1200 bytesで、不正長・未知version/type・過大countを拒否する。

送信元IPv4アドレスとポートに対するtruncated HMAC cookieをCreate/Join等の前に要求し、
UDP spoofingと応答増幅を抑える。参加tokenを使う更新要求は登録時と同じ送信元からだけ受理する。
送信元単位のtoken bucketは10要求/秒、burst 20で、ルーム上限は32人、無通信30秒で参加者を失効する。

テストは次で実行する。

```text
python3 -m unittest tools.rendezvous.test_server
```

## 対象外と検証状態

この実装はIPv4専用であり、TURN/ゲーム通信リレー、IPv6、ゲーム通信の暗号化、アカウント認証、
ランデブー状態の永続化・高可用化を提供しない。

C++ NAT codecとランデブーサービスのローカル自動テストは実装済みである。NAT越え機能の
完了判定には、公開配置したSTUN/ランデブーを使う異なる2回線での接続と、NAT越し3ピアでの
ホスト移行・PeerId維持の実地検証が別途必要である。
