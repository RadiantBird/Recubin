AvatarBatch NaN 注入の実機検証
================================

目的
  悪意ある Host が NaN 座標の AvatarBatch を送っても、Client がハングしないこと
  (NaN エントリだけを捨てて動作を続けること) を実機で確かめる。

構成
  assets/scenes/AvatarBatchNaN.yaml
    - NetworkTest と同じ StarterCharacter / Users
    - 床 (Plate)、四隅の目印 (赤/緑/青/黄の柱)
    - 落下する動的Cube 3つ (FallingBoxA/B/C) … Host 物理の同期が続いているかの目安
  run_avatar_batch_nan.ps1
    - Host を --direct-host <port> --debug-inject-nan-avatar-batch で起動
    - 3秒後に Client を --direct-connect 127.0.0.1:<port> で起動

  --debug-inject-nan-avatar-batch は検証専用の起動フラグ。Host のときだけ有効で、
  通常の AvatarBatch とは別に、全エントリの位置・速度を NaN にした AvatarBatch を
  約1秒ごと (通常バッチ20回ごと) に追加で送る。正規の姿勢は通常バッチで届いている。

手順
  1. python build.py build
  2. powershell -ExecutionPolicy Bypass -File TestCases\NetworkTest\run_avatar_batch_nan.ps1
     (別PC間で試す場合: Host側で -HostOnly、Client側で -ClientOnly -HostAddress <HostのIP>)
  3. 両方のウィンドウでキャラクターを歩かせ、30秒ほど様子を見る。

期待する結果
  Host のコンソール
    起動時:  [RCBN_WARN] [DebugInject] NaN AvatarBatch injection is ENABLED ...
    約1秒ごと: [DebugInject] sent NaN AvatarBatch #N (entries=2)
  Client のコンソール
    約1秒ごと: [RCBN_WARN] Replication: AvatarBatch entry with non-finite pose/velocity ignored (id=...)
              (Host 分と自分の分で、1回の注入につき entries 個)
  画面
    - Client が固まらない (フレームが進み、操作できる)
    - Client から見た Host のアバターが原点へ飛んだり消えたりしない
    - Client 自身のキャラクターが補正で吹き飛ばない
    - FallingBox が両方で同じように落ちて止まる

失敗のサイン
  - Client のウィンドウが応答なしになる / フレームが止まる  → 受信側の NaN 検証が効いていない
  - "ignored" の WARN が出ずに Host アバターが消える/原点へ飛ぶ → NaN が姿勢に適用されている
  - Host 側に "[DebugInject] sent" が出ない → フラグが Host として渡っていない
    (--direct-host / --host と一緒に指定したか確認)

補足
  - -NoInject を付けると注入なしで起動する。注入以外のログ(replay correction 等)が
    注入の有無で変わるかを比べるための基準用。
  - 修正前の Client(古い RecubinEngine.exe)で同じ Host に接続すると、NaN を受けた直後に
    物理ステップでハングするはず(修正前の再現)。比較したい場合のみ。
  - Client 起動時に Gyro の旧プロパティに関する RCBN_ERROR が出るのは、NetworkTest と共通の
    StarterCharacter に由来する既存の警告で、本検証とは無関係。
