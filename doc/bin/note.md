デストラクタが走るのが遅すぎて破棄前の処理ができない。
removeChildを改造するか
結論
->破棄前の処理(Parent == nullptr)はonAncestorChangeに書き込めばいい

---
# どうやってネットワークをテストする？

現状の接続方法を説明します。ポイントは「配布ランタイム（RecubinEngine.exe）のコマンドライン引数で起動時に決める」です。エディター（Recubin.exe）からは接続できません。

起動方法
まず実行フォルダに startup.yaml（開始シーンの指定）が必要です。ネットワークを使うならシーン側の System プロパティ UseNetwork: true にしておきます（Script/LocalScript の実行区別に必要）。

ホストになる（サーバー役）:


RecubinEngine.exe --host 7777
ポート省略時は 7777 です。

クライアントとして参加する:


RecubinEngine.exe --connect 192.168.x.x 7777
同じPCでテストするなら --connect 127.0.0.1 7777 です。
↓
RecubinEngine.exe --connect 127.0.0.1 7777

オプションで --listen-port <port> を付けると「自分がホスト昇格したときに使うポート」を指定できます（省略時は接続先と同じポート。通常は省略でOK）。

つながると何が起きるか
クライアントは接続確立時に自動で Hello を送り、ホストから PeerId が割り当てられます（ホスト=1、以降 2, 3, …）
全員に「ロスター」（全ピアのID・CPUスコア・レイテンシ一覧）が毎秒配布されます
ホストのプロセスが落ちると（約30秒のタイムアウト検知後）、残ったメンバーの中でCPUスコアが一番高い人が自動でホストに昇格し、他は自動で再接続します。操作は不要です
スクリプト側から見ると
Script はホストでのみ実行、LocalScript は全員で実行されます（UseNetwork: true のとき）
Luauから状態を見るには:

print(System.NetworkRole)   -- "Offline" / "Host" / "Client"
print(System.LocalPeerId)   -- 自分のピアID
System.NetworkRoleChanged:Connect(function(oldRole, newRole)
    print("ロールが変わった: " .. oldRole .. " -> " .. newRole)
end)
現状の制限
LAN / localhost 前提です。インターネット越し（NAT配下同士）は未対応
まだワールドの同期はありません。接続・ロスター共有・ホスト移行の基盤だけで、キャラクターやオブジェクトの位置が相手に見えるわけではない段階です（現状はモックのチャットとダミー座標がログに出るだけ）
ゲーム側からデータを送る手段（NetworkEvent）も未実装なので、「実際に一緒に遊べる」にはレプリケーションと NetworkEvent が次のステップになります
試すなら、同じPCでコンソールを2つ開いて --host 7777 と --connect 127.0.0.1 7777 を起動し、ログに roster や [chat] Hello from ... が出るのを見るのが一番手軽です。
---

# NAT越えまでやった最新の接続方法

現在、実際に接続成功まで確認済みなのは「Tailscaleによる直接接続」です。公開STUN＋ランデブー方式も実装済みですが、異なる回線での実地検証は未完了です。
事前準備
RecubinEngine.exeと同じ作業フォルダにstartup.yamlを置く。
開始シーンのSystem.UseNetworkをtrueにする。
エディターのRecubin.exeではなく、ランタイムのRecubinEngine.exeを起動する。
現在のビルド成果物なら、PowerShellで次の場所へ移動します。
cd build\Release
既存の[startup.yaml](/mnt/c/Users/Ryarta/Documents/Recubin/build/Release/startup.yaml)はassets/scenes/void.yamlを指定しており、このシーンはすでにUseNetwork: trueです。
推奨：Tailscale直接接続
両端末を同じtailnetへ参加させ、まずホスト側のTailscale IPv4を確認します。
tailscale ip -4
tailscale status
ホスト側：
.\RecubinEngine.exe --direct-host 41001
クライアント側：
.\RecubinEngine.exe --direct-connect 100.90.255.77:41001 --listen-port 41002
100.64.0.10はホストのTailscale IPv4またはMagicDNS名に置き換えます。
Windows Defender FirewallではUDP受信を許可してください。ホスト移行も試す場合、クライアントの待受ポートも必要です。
New-NetFirewallRule `
  -DisplayName "Recubin Network" `
  -Direction Inbound `
  -Protocol UDP `
  -LocalPort 41001-41003 `
  -Action Allow
3台目は重複しないポートで起動します。
.\RecubinEngine.exe --direct-connect 100.64.0.10:41001 --listen-port 41003
ホスト移行を追いやすくするには、全端末で固定の--listen-portを使用してください。
ルームコード接続（STUN＋ランデブー）
公開IPv4で到達できるSTUNサーバーとランデブーサーバーが必要です。ランデブーは次のように起動できます。
python3 tools/rendezvous/server.py \
  --bind 0.0.0.0 \
  --port 3479 \
  --ttl 30 \
  --secret "十分に長い固定秘密値"
UDP 3479を外部へ開放します。これはSTUNサーバーではないため、UDP 3478のRFC 8489対応STUNサービスも別途必要です。
ホスト：
.\RecubinEngine.exe `
  --host `
  --stun stun.example.net:3478 `
  --rendezvous rendezvous.example.net:3479 `
  --listen-port 41001
画面／コンソールに表示された8文字のルームコードをクライアントへ渡します。
クライアント：
.\RecubinEngine.exe `
  --connect ABCD1234 `
  --stun stun.example.net:3478 `
  --rendezvous rendezvous.example.net:3479 `
  --listen-port 41002
接続先はstartup.yamlにも設定できます。
StartScene: assets/scenes/void.yaml
StunServer: stun.example.net:3478
RendezvousServer: rendezvous.example.net:3479
その場合の起動は短くできます。
.\RecubinEngine.exe --host
.\RecubinEngine.exe --connect ABCD1234
CLI指定はstartup.yamlより優先されます。--listen-port 0または省略時はOSが空きUDPポートを選びます。
確認ポイントと既知の制限
接続後はコンソールへの文字入力でチャット疎通を確認できます。ログではConnected、PeerId、Rosterを確認します。
--hostと--connectは同時指定不可。
--connectにはIPアドレスではなく8文字のルームコードを渡す。
IPv4専用。IPv6、TURN／通信リレーは未対応。
対称NATなどで直接経路を作れない場合はPunchTimeoutになる。
Tailscale直接接続で失敗した際、状態がConnectingのまま残る既知の診断不備があるため、その場合は手動終了する。
公開STUN＋ランデブー方式はローカル自動テスト済みですが、異なる実回線での最終検証はまだ未完了。
詳細は[NatTraversal.md](/mnt/c/Users/Ryarta/Documents/Recubin/doc/Network/NatTraversal.md)と[Network Probe手順](/mnt/c/Users/Ryarta/Documents/Recubin/tools/network_probe/README.md)にまとまっています。

## Macでのビルド

Apple Silicon（arm64）のMacに必要なツールを入れ、リポジトリのルートでビルドします。

```sh
brew install cmake glfw glew git
python3 build.py build Release
```

`Debug`も指定できます。PhysX 5.6.1は初回ビルド時に自動取得・パッチ適用・ビルドされるため、
ネット接続が必要で時間がかかります。自動取得はApple Silicon Mac限定です。
既にビルド済みのPhysXを使う場合は、静的ライブラリがあるディレクトリを
`RECUBIN_PHYSX_MAC_DIR=/path/to/physx/libs`で上書きできます。
