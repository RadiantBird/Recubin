# Process

`Process` は Windows のローカル C++ DLL をテストプレイまたはパッケージ済みゲームで実行する Instance です。ネットワーク経由では実行されません。DLL はエンジンと同じプロセス内のネイティブコードとして動作します。

## プロパティ

| プロパティ | 型 | 説明 |
|---|---|---|
| `Path` / `ContentPath` | string | DLL のソース C++ ファイル。Scene の asset base から解決します。 |
| `DllPath` | string | ビルド済み DLL の参照。Scene の asset base から解決します。 |
| `Enabled` | bool | `false` の Process は準備・実行されません。既定値は `true` です。 |

実行状態、DLL ハンドル、待機状態、watchdog は Play セッションの `ProcessRuntimeManager` が所有します。Scene には上記の設定だけを保存します。

## C++ SDK

公開 API は `app/include/recubin.h` から利用します。`InstanceRef<T>` はエンジンが所有する Instance への非所有・世代検証付き参照です。DLL 側に Instance の実体は複製されません。削除済み Instance への操作は失敗を報告します。

```cpp
#include <recubin.h>

RCBN_PROCESS_START {
    recubin::InstanceRef<recubin::Cube> cube =
        recubin::Instance::New<recubin::Cube>();
    cube.Name.Set("MovingCube");
    cube.Color.Set(recubin::Color4(1, 0, 0, 1));

    std::optional<recubin::InstanceRef<recubin::System>> system =
        recubin::GetSystem();
    if (!system) return;
    cube.Parent.Set(*system);

    recubin::Connection update = system->Heartbeat.Connect(
        [](double deltaSeconds) {
            (void)deltaSeconds;
        });
    while (!recubin::task::shouldStop()) {
        recubin::task::wait(0.016);
    }
}
```

プロパティは `.Get()` / `.Set(value)` で読み書きします。`FindChild<T>(name)` は名前で直接の子を検索し、呼び出し側が型を指定します。正しい型へキャストできる前提の API です。`FindChildWithClass<T>(name)` はクラス名がちょうど `T` と一致する場合だけ `std::optional<InstanceRef<T>>` を返します。`Parent.Get<T>()` は親への任意の型付き参照、`Parent.GetAs<T>()` は実際の親が `T` と互換な場合の参照を返します。`Parent.Set(ref)` と `Parent.Clear()` で親を変更します。

`Instance::New<T>()` は型付き参照を返します。新規 Instance は親へ設定されるまでホストが一時所有します。`GetSelfProcess()` は実行中の Process 参照を返し、`GetSystem()` は System が存在しない場合に空の optional を返します。

現在の値 API は bool、int、float、string、`Vector3`、`Color4`、`CFrame` です。公開プロパティは System の `EnableExecutingProcess`、`UseNetwork`、`ProcessWatchdogTimeoutSeconds`、Process の `Enabled` / `Path` / `DllPath`、BaseCube の `CFrame` / `Position` / `Size` / `Color` / `Anchored` / `CanCollide` / `CanTouch` と Instance の `Name` / `Parent` です。読み取り専用プロパティへの書き込みや型不一致は失敗します。

`System.Heartbeat.Connect(function)` は関数ポインタ、ラムダ、キャプチャ付きラムダを受け取ります。`BaseCube.Touched.Connect(function)` と `TouchEnded.Connect(function)` の引数は `InstanceRef<BaseCube>` です。接続を切るときは `Connection::Disconnect()` を呼びます。Connection の破棄だけでは切断されません。Process 停止時にはすべての接続と DLL 側コールバックを解放します。

`task::wait(seconds)` は Process を一時停止し、指定時間後に再開します。停止要求で待機が解除されたときも通常どおり戻るため、ループ条件で `task::shouldStop()` を再確認してください。待機しない無限ループは停止できず、watchdog によりエンジンを終了します。Heartbeat や Touch callback 内から `task::wait()` を呼ぶことはできません。

`start()` は必須です。`stop()` とユーザー定義の `bindHost()` は任意です。`RCBN_PROCESS_START`、`RCBN_PROCESS_STOP`、`RCBN_PROCESS_BIND_HOST` マクロを使用できます。ホストが作る `rcbn_process_bind_host` / `rcbn_process_unbind_host` は SDK 内部の実行コンテキスト管理用です。

## ビルドと Play

Studio は有効な Process の DLL をソースと SDK ヘッダーの更新時刻と比較します。DLL が存在しない、または依存ファイルより古い場合は、設定済みのコンパイラで自動ビルドを試みます。初回は `cl.exe`、`clang-cl.exe`、`g++.exe`、`gcc.exe` のいずれかを選びます。選択は Settings の「Select Process Compiler...」から変更できます。MSVC と clang-cl は Visual Studio の C++ 環境を使い、GNU コンパイラは選択した実行ファイルの環境で Windows DLL を生成します。コンパイラが見つからない、環境初期化またはビルドに失敗した場合はログを表示し、手動ビルド後の再試行を案内します。ビルド成功後にのみ成果物を置き換えます。古い DLL はソースまたは SDK ヘッダーより古ければ実行しません。

Play 開始前に Scene が dirty の場合は保存を確認します。ビルド失敗や DLL のロード失敗では Play を開始しません。パッケージ済みゲームでは同梱 DLL のみを使い、ユーザー環境のコンパイラは呼び出しません。

Process の DLL ビルドでは C++ ソースと `recubin.h` を含む SDK ヘッダー群を使用します。DLL ABI は `RCBN_PROCESS_HOST_ABI_VERSION` でバージョン管理します。DLL とホストが異なる ABI の場合は再ビルドしてください。

## 権限とネットワーク

`System.EnableExecutingProcess` が `true` の場合だけ Process を実行できます。配布ゲームではネイティブ DLL 実行の初回許可が必要です。`System.UseNetwork=true` の場合、Process は実行不可です。実行許可と watchdog timeout は C++ SDK から読み取り専用です。`System.ProcessWatchdogTimeoutSeconds` は正の有限値で、既定値は 2 秒です。

## 停止と制限

Play 停止、Scene 切替、またはエンジン終了では停止要求を送り、`start()` が `task::wait()` から戻った後に `stop()` を呼びます。その後、ホスト参照を無効化して DLL を解放します。DLL が待機へ戻らない場合、同一プロセス内のネイティブコードを安全に中断できないため、watchdog は Windows の `TerminateProcess` でエンジンを終了します。

- 現在のローカル実行と自動ビルドは Windows のみです。自動ビルドで選択できるコンパイラは MSVC、clang-cl、MinGW GCC です。
- DLL はエンジンと同じプロセス内で動作し、Luau のメモリ安全境界はありません。
- Scene 切替、Instance 削除、または DLL 解放後に保持した `InstanceRef` は無効です。
- 物理専用 API、描画 API、ネットワーク API は公開していません。
- 無限ループ、メモリ破壊、または強制終了から編集内容を復旧する保証はありません。
