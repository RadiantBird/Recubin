# TextFile

`include/Instances/TextFile.hpp`

`TextFile`は同梱されたテキストseedと、実行時のポータブルroot内保存領域を結び付ける
`PhysicalFileInstance`である。エディターではInsert Objectから新規`.txt`の作成または
既存`.txt`の選択を行う。新規作成は既存ファイルを上書きせず、UndoではScene Instanceだけを
取り消して物理ファイルを削除しない。

| プロパティ | 説明 |
|---|---|
| `ContentPath` | 配布パッケージに同梱する初期seed。YAMLではPhysicalFileInstanceのPathとして保存される |
| `StorageId` | UUID。ユーザー領域のmutable copyを識別し、複製時に再生成される。エディターでは読み取り専用 |
| `Content` | 全文の読み書き値。I/O API権限なしで利用できる |

Luauから`Instance.new("TextFile")`や`Clone()`で新しいファイルを増やすことはできない。
Packagerは既存の`ContentPath`追跡を使ってseedを同梱する。

初回アクセス時だけ`ContentPath`のseedを`StorageId`保存先へコピーし、以後はユーザー保存を優先する。
Contentはroot直下の`textfiles/<StorageId>.txt`へ保存され、Editorと配布ランタイムはそれぞれの起動rootによって分離される。Contentは最大128 MiBで、読み書きにI/O API権限を必要としない。
`StorageId`はYAMLには保存するがLuauへ公開しない。Luauの`Instance.new("TextFile")`および`TextFile:Clone()`は拒否される。
