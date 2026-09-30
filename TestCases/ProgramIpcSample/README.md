# ProgramIpcSample

`Program` インスタンス（IPC）の使い方を、読んで・実行して理解するためのサンプルです。
Recubin から子プロセス（exe）を起動し、標準入出力で文字列をやり取りします。
仕様の詳細は [doc/Instances/Program.md](../../doc/Instances/Program.md) を参照してください。

## 中身

| ファイル | 役割 |
|---|---|
| `ProgramIpcSample.rcbn` | シーン。`Program`（`EchoServer`）、ログ表示の`TextLabel`、`Script`を持つ。`EnableIPCAPI: true` |
| `scripts/ProgramIpcSample.luau` | Luau側。`Start`→`Call`→`Send`/`Receive`→タイムアウト→`Disconnect`/`Connect`→`Close`を順に実行 |
| `src/echo_server.cpp` | 子プロセスのソース（C++）。行単位のコマンドサーバー |
| `assets/echo_server.exe` | `echo_server.cpp` をビルドした exe（`Program.ContentPath` が指す） |
| `build.bat` | `echo_server.cpp` → `assets/echo_server.exe` のビルド（Visual Studio が必要） |

## 実行

1. リポジトリのルートをカレントにして `Recubin.exe` を起動する（`ContentPath` はルート相対）。
2. `TestCases/ProgramIpcSample/ProgramIpcSample.rcbn` を開いて Play する。
3. 画面の `Log` ラベルとコンソールに、各ステップの結果が出る。

ヘッドレスでも確認できる（ルートで実行）:

```text
build\Release\RecubinTest.exe TestCases/ProgramIpcSample/ProgramIpcSample.rcbn
```

期待される出力（抜粋）:

```text
ping            -> pong
add 2 3.5       -> 5.5
echo (2 lines)  -> first line / second line
Receive right after Send -> nil
Receive later           -> SENT WITH SEND
Call(slow, 0.5) ok=false  error=Program:Call timed out
Receive after Connect  -> buffered while disconnected
exit code = 9
```

exe を作り直すときは `build.bat` を実行する（自動で `vcvars64.bat` を探す）。

## 仕組み

```text
Luau (Program)                          echo_server.exe
  Call("ping") ── "ping\n" ──stdin──▶  getline → handle → "pong\n"
        ◀────────── "pong\n" ──stdout──
```

- **1 行 = 1 メッセージ。** 要求は標準入力へ1行、応答は標準出力へ1行。子は必ず「1 要求につき 1 行」返し、
  書くたびに `flush` する。
- **改行は自動でエスケープ。** メッセージ内の改行は `\n`、`\` は `\\`、CR は `\r` として送られる。
  Luau側は普通の改行入り文字列として扱える（手順3で確認できる）。子がテキストを解釈しない限り、
  エスケープされたまま返せば元に戻る。
- **応答は要求の順番どおり（FIFO）。** `Call` の応答は待っているコルーチンへ、`Send` の応答は受信キューへ入る。
  1 要求に対して 2 行以上返したり、勝手に行を出力したりすると、以降の応答がずれる。
- **`echo_server` のコマンド:** `ping` / `echo <text>` / `upper <text>` / `add <a> <b>` / `count` / `slow`
  （2秒待つ。タイムアウトの実験用）。その他は `error: ...` を返す。
- **終了:** `Close` は標準入力を閉じる。子は `getline` が終わったらループを抜けて終了する。
  猶予（既定3秒）内に終わらなければ強制終了される。`echo_server` は処理した要求数を終了コードにする。

## メソッドの要点

| メソッド | 動作 |
|---|---|
| `Start()` | 起動。起動済みならエラー |
| `Call(request, timeout?)` | 応答を待つ（既定5秒）。**コルーチンが止まるだけでフレームは止まらない**。失敗はLuauエラー |
| `Send(request)` / `Receive()` | 送るだけ / 届いていれば取り出す（無ければ `nil`、待たない） |
| `Disconnect()` / `Connect()` | こちらの送受信だけを止める/再開する。プロセスは動いたまま。切断中に届いた `Send` の応答は溜まる |
| `Close(grace?)` | 終了を待って終了コードを返す |

## 自分の exe に置き換えるには

1. `src/echo_server.cpp` を参考に、「標準入力から1行読む → 処理 → 標準出力に1行書いて flush」を繰り返す exe を作る。
2. 標準入力が閉じたら終了する（これが `Close` の合図）。
3. シーンの `Program.ContentPath` をその exe のパスにする（ルート相対）。
   エディターなら Explorer の Insert Object → `Program` から追加して、Path を選ぶ。

## 注意

- `System.EnableIPCAPI` が `false` だと `Start` などはエラーになる（このシーンは `true`）。
- 対応OSはWindowsのみ。
- 子が標準入力を読まずに大量に書かせると詰まる。大きなデータを送る使い方には向かない（1メッセージ1MiBまで）。
- Packager での配布（exe の同梱）はこのサンプルでは検証していない。
