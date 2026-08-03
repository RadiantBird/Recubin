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

## CI/CD

pushとPull Requestでは、Pythonランデブーサーバーのテストに加え、Windows latestと
macOS latestでRelease版をビルドする。各ビルドではloopback上にdirect Host/Clientを起動し、
2ピアの`READY`と双方向チャットを確認してから、OS別zipをGitHub Actionsのartifactへ保存する。

ローカルでも、ビルド済みの実行ファイルを指定して同じ疎通テストを実行できる。

```powershell
python tools/network_probe/test_local.py `
  build-network-probe/Release/RecubinNetworkProbe.exe
```

```sh
python3 tools/network_probe/test_local.py build-network-probe/RecubinNetworkProbe
```

`v`で始まるタグ（例: `v1.0.0`）をpushすると、テスト成功後に対応するGitHub Releaseを
必要に応じて作成し、Windows版とmacOS版のzipを更新する。

## 起動

### Tailscaleで直接接続

VPSを用意しない場合は、WindowsとMacを同じTailscaleネットワーク（tailnet）へ参加させ、
Tailscale IPv4またはMagicDNSのホスト名で直接接続できる。このモードはSTUN、
ランデブー、ルームコードを使用しない。

たとえばTailscale IPv4が`100.64.0.10`のWindowsをHostにする。

```powershell
.\build-network-probe\Release\RecubinNetworkProbe.exe `
  --direct-host 41001 `
  --expect-peers 2
```

MacからWindowsへ参加する。`--listen-port 0`は省略でき、その場合もOSが空きポートを選ぶ。
ホスト移行を試す場合は、固定ポート（次の例では`41002`）を指定するとログを追いやすい。

```sh
./build-network-probe/RecubinNetworkProbe \
  --direct-connect 100.64.0.10:41001 \
  --listen-port 41002 \
  --expect-peers 2
```

RecubinEngineも同じ引数を使用する。

```powershell
.\RecubinEngine.exe --direct-host 41001
```

```sh
./RecubinEngine --direct-connect 100.64.0.10:41001 --listen-port 41002
```

Windows Defender FirewallとmacOSファイアウォールでは、各端末が使用するUDPポートの
受信を許可する。最初のHostだけでなく、ホスト移行候補になるClientの
`--listen-port`も受信可能にする必要がある。ホスト移行へ参加する全端末は同じtailnetに
所属し、相互通信をTailscale ACLで許可しておく。

直接接続モードはランデブーが発行する参加tokenを使用しないため、指定ポートへ到達できる
tailnet内の端末を信頼する構成である。参加者を制限する場合はTailscale ACLと端末管理で
接続範囲を制御する。IPv6形式は受け付けず、`IPv4:port`または`hostname:port`を指定する。

#### 実機テスト手順

次の例ではWindows Hostを`100.64.0.10:41001`、Mac Clientの待受をUDP `41002`、
3台目をUDP `41003`とする。最初に全端末が同じtailnetへ接続済みであることと、各端末の
Tailscale IPv4・ホスト名を確認する。MagicDNSが有効ならIPv4の代わりにそのホスト名を
`--direct-connect`へ指定できる。

```powershell
tailscale ip -4
tailscale status
```

```sh
tailscale ip -4
tailscale status
```

2台テストでは、まずWindowsでHostを起動する。

```powershell
.\build-network-probe\Release\RecubinNetworkProbe.exe `
  --direct-host 41001 `
  --duration 60 `
  --message-interval 2 `
  --expect-peers 2
```

続けて60秒以内にMacから参加する。

```sh
./build-network-probe/RecubinNetworkProbe \
  --direct-connect 100.64.0.10:41001 \
  --listen-port 41002 \
  --duration 30 \
  --message-interval 2 \
  --expect-peers 2
```

両端末で`state ... -> Connected`、0以外の`peer`、`roster peers=2`、
相手が送った`chat`を確認する。`--duration`後の終了コードは両方とも0であること。
直接接続では`room-code`を表示せず、STUN・ランデブー関連ログも成功判定には不要である。

3台でホスト移行を確認する場合は、各端末を別ターミナルで次の順に起動する。

```powershell
.\build-network-probe\Release\RecubinNetworkProbe.exe `
  --direct-host 41001 `
  --duration 0 `
  --message-interval 2 `
  --expect-peers 3
```

```sh
./build-network-probe/RecubinNetworkProbe \
  --direct-connect 100.64.0.10:41001 \
  --listen-port 41002 \
  --duration 0 \
  --message-interval 2 \
  --expect-peers 3
```

```sh
./build-network-probe/RecubinNetworkProbe \
  --direct-connect 100.64.0.10:41001 \
  --listen-port 41003 \
  --duration 0 \
  --message-interval 2 \
  --expect-peers 3
```

全端末が`READY expected-peers=3`になった後、Windows Hostを`Ctrl-C`で終了する。
残存端末で`Connected -> Migrating -> Connected`、いずれかの
`role Client -> Host`、移行後の`chat`継続を確認する。各端末のPeerIdと
`listen-port`（`41002`または`41003`）は移行前後で変わらない。

接続できない場合は、Clientから`tailscale ping 100.64.0.10`（またはMagicDNS名）を実行し、
Tailscale経路を先に確認する。Windowsでは管理者PowerShellから、テスト用にUDP
`41001`〜`41003`の受信規則を作成できる。

```powershell
New-NetFirewallRule -DisplayName "Recubin Network Probe" `
  -Direction Inbound -Protocol UDP -LocalPort 41001-41003 -Action Allow
```

macOSではファイアウォールの受信確認で`RecubinNetworkProbe`を許可する。さらにTailscale
ACLが端末間通信を許可していること、接続先がIPv4または`hostname:port`形式であること、
各Clientの`--listen-port`が端末間で重複していないことを確認する。

ログを保存する場合は次のように実行する（必要な起動引数は上の例と同じように続ける）。

```powershell
.\build-network-probe\Release\RecubinNetworkProbe.exe --direct-host 41001 `
  --duration 60 --message-interval 2 --expect-peers 2 2>&1 |
  Tee-Object -FilePath network-probe-windows.log
```

```sh
set -o pipefail
./build-network-probe/RecubinNetworkProbe --direct-connect 100.64.0.10:41001 \
  --listen-port 41002 --duration 30 --message-interval 2 --expect-peers 2 \
  2>&1 | tee network-probe-mac.log
```

WindowsのVisual Studio構成では実行ファイルは通常
`build-network-probe\Release\RecubinNetworkProbe.exe`、macOSの単一構成ビルドでは
`build-network-probe/RecubinNetworkProbe`にある。別のCMake generatorや構成を使った場合は、
実際の出力先に合わせてこの部分だけ置き換える。

### STUN・ランデブーで接続

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
