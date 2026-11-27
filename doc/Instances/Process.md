# Process

`Process` は、Windows のローカル C++ DLL をテストプレイまたはパッケージ済みゲームから実行する Instance です。ネットワークプレイでは実行されません。Luau の `Script` と異なり、DLL はネイティブコードとして同一エンジンプロセス内で動作します。

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `ContentPath` / `Path` | string | DLL のソース C++ ファイル。`PhysicalFileInstance` のファイル参照として保存されます。 |
| `DllPath` | string | ビルド済み DLL の参照。シーンの asset base から解決されます。 |
| `Enabled` | bool | `false` の Process は準備・実行されません。既定値は `true` です。 |

`Process` の実行状態、DLL ハンドル、コルーチン、watchdog は Instance に保存されず、Play セッションの `ProcessRuntimeManager` が所有します。clone と Scene YAML は上記の永続プロパティだけを複製します。

## DLL の ABI

公開ヘッダーは `include/recubin_process.h` です。DLL は次の C リンケージ関数をエクスポートします。

```cpp
#include <recubin_process.h>

RCBN_PROCESS_START {
    while (!recubin::process::task::shouldStop()) {
        recubin::process::task::wait(0.016);
    }
}

RCBN_PROCESS_STOP {
    // 任意の終了処理
}

RCBN_PROCESS_BIND_HOST {
    // 任意。受け取った API は DLL がロードされている間だけ利用する。
}
```

`start()` は必須です。`stop()` と `bindHost()` は任意です。`start()` 内で継続処理を書く場合は `task::wait(seconds)` を呼び、エンジンへ制御を返してください。`task::shouldStop()` は停止要求を確認します。待機も API 呼び出しも行わない無限ループは停止できず、watchdog によりエンジンごと終了します。

`bindHost()` が受け取る API は ABI version 4 です。現在公開されている操作は、System の取得、直接の子の名前検索、厳密なクラス名検索、Instance の有効性・型判定、名前の取得・変更、Instance の生成、親設定です。イベント、任意プロパティの読み書き、Workspace の汎用操作、ネットワーク API はまだ公開されていません。

`recubin_process_api.h` の `InstanceRef` はエンジン管理の非所有ハンドルです。`FindChild(name)` は `std::optional<InstanceRef>`、`FindChildWithClass<U>(name)` は指定名の直接の子が `U::ClassName()` と厳密一致した場合だけ `std::optional<U>` を返します。ハンドルは Scene 切替、削除、DLL 解放後には無効になるため、`valid()` を確認してください。

## ビルドと Play

Studio で Play を開始すると、Enabled Process ごとに DLL の存在と、ソース・指定ヘッダーの更新時刻を確認します。DLL がない、または依存ファイルより古い場合、開発環境の MSVC `cl.exe` で一時 DLL をビルドします。ビルド成功後だけ DLL を置き換え、失敗時に古い DLL は実行しません。MSVC が見つからない、コンパイルに失敗した、DLL をロードできない場合は Play を開始しません。

ビルド引数はシェル文字列として連結せず、子プロセスへ引数配列で渡します。UTF-8 のシーン・アセットパスは Windows の UTF-16 API 経由で扱います。配布ゲームでは `allowBuild=false` の実行設定を使い、パッケージへ同梱された DLL を検証して実行します。プレイヤー側でソースからビルドする動作は提供しません。

## 権限とネットワーク

`System.EnableExecutingProcess` が `true` の場合だけ Process を実行できます。配布ゲームではネイティブ DLL 実行の初回許可が必要です。`System.UseNetwork=true` の場合、Process は実行不可です。Process の実行可否と watchdog timeout は Luau から変更できません。`System.ProcessWatchdogTimeoutSeconds` は正の有限値を秒単位で指定し、既定値は 2 秒です。不正値は警告を出して拒否します。

## 停止と異常終了

Play 停止、Scene 切替、またはエンジン終了では停止要求を送り、`start()` が `task::wait()` から戻った後に任意の `stop()` を呼びます。その後、登録されたホスト参照を無効化して DLL を解放します。DLL が戻らない場合、同一プロセス内のネイティブコードを安全に中断できないため、watchdog が低水準のエラーを出力して Windows の `TerminateProcess` でエンジンを終了します。watchdog の既定値は 2 秒ですが、現在 System の Scene プロパティとして設定する API は未実装です。

## 制限

- Windows / MSVC のローカル実行のみです。
- DLL はエンジンと同じプロセスで動作し、Luau のメモリ安全境界はありません。
- DLL が保持したホスト API ポインターは DLL 解放後に使用できません。
- Process のイベント、任意プロパティ、物理・描画専用 API、ネットワーク通信は未実装です。
- 無限ループ、未処理例外、メモリ破壊から編集内容を復旧する保証はありません。Play 開始前に dirty な Scene は保存確認を行ってください。
