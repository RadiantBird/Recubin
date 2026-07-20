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