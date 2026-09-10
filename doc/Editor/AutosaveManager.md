# AutosaveManager

`AutosaveManager` owns one editor session under
`<storage root>/.autosave/<scene filename>/`. The caller supplies the storage
root explicitly. On Windows, `main.cpp` resolves it to the directory containing
the running `Recubin.exe`. A flat macOS portable distribution uses that same
rule when the executable directory contains the `.autosave` marker and required
Studio resources; development launches keep their original working directory.
Existing Autosave data below an old working directory is not
migrated, scanned, or removed. Recovery is debounced by one second;
periodic snapshots default to five minutes and five generations. Writes are
performed synchronously through a same-directory temporary file followed by an
atomic replacement. A session lock and recovery document are removed by
`endSessionNormally` or `discardRecovery`, while snapshots are retained.

Crash candidates are accepted only when both the lock and recovery document
exist and the recovery can be loaded by `SceneLoader`. The currently active
session directory is excluded from crash discovery. A lock whose recovery has
not been created (or is already absent) is a normal non-candidate and is
ignored without an error log; filesystem inspection failures and corrupt
recovery documents are still logged with their paths.

実ファイルI/O失敗はoperationと絶対pathをcallbackへ渡す。同一operation/pathの連続失敗は
一度だけ通知し、一度成功した後の再失敗は再通知する。未作成Recovery、破損YAML、Scene serialization
失敗は権限障害とは区別してログだけに残す。
