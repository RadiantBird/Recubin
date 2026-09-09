# AutosaveManager

`AutosaveManager` owns one editor session under
`<project>/.autosave/<scene filename>/`. Recovery is debounced by one second;
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
