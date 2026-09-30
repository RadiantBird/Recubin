# Program

`include/Instances/Program.hpp` / `src/Instances/Program.cpp`

exeファイルを所有し、子プロセスと標準入出力パイプ越しに文字列を交換する`PhysicalFileInstance`。
`TextFile`と同じく「シーンが所有する」Instanceで、エディターのInsert Objectまたはシーンロードからのみ生成される
（`luaCreatable=false`。Luauの`Instance.new("Program")`は拒否される）。

| プロパティ | 説明 |
|---|---|
| `Path` | 起動するexe。YAMLではContentPathとして保存される。Luauからは読み取り専用 |

## 通信仕様

- 要求/応答はUTF-8のstring。**1行=1メッセージ**で、`\` → `\\`、改行 → `\n`、CR → `\r`にエスケープして送る。
  子プロセス側は1行読んで1行書く（末尾`\r\n`も可）。
- 各要求（`Call`/`Send`）は応答を1行ずつFIFOで消費する。`Call`の応答は待機中のコルーチンへ、
  `Send`の応答と、要求に対応しない行は受信キューへ入り`Receive`で取り出せる。
- 1メッセージは1 MiBまで。受信キューは4096件まで（超過分は古い順に破棄して警告）。
- 標準エラーは破棄される。

## Luauメソッド

すべて`System.EnableIPCAPI`が必要（無効なら実行時エラー）。失敗はLuauエラーで、`pcall`で捕捉できる。

| メソッド | 動作 |
|---|---|
| `Start()` | プロセスを起動する。起動済みならエラー（先に`Close`する） |
| `Call(request, timeout?)` | 要求を送り、**コルーチンをyield**して応答を待つ（フレームは止まらない）。応答stringを返す。`timeout`は秒（既定5）。タイムアウト・切断・プロセス終了・`Close`はエラー |
| `Send(request)` | 要求を送るだけ（非同期） |
| `Receive()` | 受信キューの先頭を返す。無ければ`nil`（非ブロッキング） |
| `Disconnect()` | こちらの送受信を止める。プロセスは生きたまま。待機中の`Call`はエラーで再開し、その応答は破棄される。子の出力は内部キューに溜まり続ける（`Send`の応答のみ保持） |
| `Connect()` | `Disconnect`した接続を復帰する。溜まった応答は`Receive`で取り出せる |
| `Close(grace?)` | stdinを閉じて終了を待ち、`grace`秒（既定3）過ぎたら強制終了する。**yield**して終了コード(number)を返す |

`Call`/`Close`はyieldするため、スクリプトまたは`task.spawn`/`task.delay`のタスクから呼ぶ必要がある
（Signalコールバック内や`require`中など、yieldできない文脈ではエラー）。

`Disconnect`は同じ匿名パイプを使い続けるため、プロセスへの「再接続」はできない
（`Connect`は同一プロセスとの通信再開）。

## ライフサイクル

- `Program::pollAll()`が`LuauEngine::update()`から毎フレーム全Programのパイプを読む（スレッドは使わない）。
  切断中でも吸い上げるため、子が出力で詰まらない。
- Play停止・シーン切替・エンジン終了の`LuauEngine::cancelAllTasks()`で、全Programのプロセスを強制終了する
  （`Program::forceCloseAll()`）。`Program`の破棄時も強制終了する。
- `clone()`は`Path`だけを引き継いだ未起動のProgramを返す。プロセスは複製されない。
- ランタイムでは`AssetGuard::allow(Path)`を通らないパスは起動できない。`ContentPath`はPackagerが同梱する。

## 注意

- 書き込みはブロッキング。子がstdinを読まずに64KBを超えて書くとフレームが止まりうる。
- 対応OSはWindowsのみ（macOSは`launchPipedProcess`が`nullptr`を返し`Start`が失敗する）。
- 既存の`IPC.Connect/Send/Receive/Close`グローバルはスタブのまま（未実装エラー）。

プロセス起動は[IPlatform](../Util/IPlatform.md)の`launchPipedProcess`（`IPipedProcess`）を使う。
