# Recubin Network Probe

`RecubinNetworkProbe`は、描画・音声・Luau・PhysXを初期化せず、Recubinの
`NetworkManager`、NAT codec、ENetだけを実行する確認用CLIである。
macOSのゲームランタイムで物理エンジンに問題がある場合も、STUN、ルーム参加、
UDPホールパンチ、チャット往復、Roster、同一ソケットでのホスト移行を確認できる。

## macOSでのビルド

Apple Command Line ToolsとCMake 3.26以降だけを必要とする。
メインエンジン用のPhysX、GLFW、GLEW、Luauは不要である。

```sh
xcode-select --install
cmake -S tools/network_probe -B build-network-probe \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-network-probe --config Release
```

## 起動

公開STUNと公開済みランデブーサービスを指定してHostを起動する。

```sh
./build-network-probe/RecubinNetworkProbe \
  --host \
  --stun stun.example.net:3478 \
  --rendezvous rendezvous.example.net:3479 \
  --listen-port 41001 \
  --expect-peers 2
```

表示された8文字の`room-code`を使い、別回線の端末から参加する。

```sh
./build-network-probe/RecubinNetworkProbe \
  --connect ABCD1234 \
  --stun stun.example.net:3478 \
  --rendezvous rendezvous.example.net:3479 \
  --listen-port 41002 \
  --expect-peers 2
```

`--listen-port 0`または省略時はOSが空きポートを選ぶ。接続後は5秒ごとに
`network-probe-<PeerId>-<連番>`を送る。受信側の`chat`ログで双方向通信を確認する。

## 異なる回線での確認

Windowsを固定回線、Macをスマートフォンのテザリングへ接続する。単に別のWi-Fiルーターへ
接続しても上流回線が同じ場合は、外側のNATが共通になるため実地検証にはならない。
macOSのファイアウォール確認が表示された場合は受信接続を許可する。STUN/ランデブー向けUDP
3478/3479に加え、`--listen-port`で指定したUDPポートを遮断しないようにする。

接続後に現在のHostを終了し、残存端末のログで次を確認する。

- `state Connected -> Migrating -> Connected`
- `role Client -> Host`または新Hostへの再接続
- 移行前後で同じ`peer`と`listen-port`
- 移行後も`chat`を継続受信

終了は`Ctrl-C`。`--duration <秒>`を指定すると自動終了する。失敗時は
`MissingConfig`などの分類を表示し、`10 + ConnectionError`を終了コードとして返す。
