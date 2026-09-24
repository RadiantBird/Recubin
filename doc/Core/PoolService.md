# PoolService

`Instance.pick(className)` は、Pool whitelist に登録されたクラスを再利用する。
利用可能なオブジェクトが無ければ、既存の `Instance.new(className)` と同じ factory
で新規生成する。whitelist 外のクラスは warning を出して Pool を使わず、通常の
`Instance.new` へフォールバックする。

`Instance.throw(instance)` は Pool から借りたオブジェクトだけを返却対象とする。
親が設定されている場合は通常の `setParent(nullptr)` 経路で Workspace の描画・物理
登録を解除してから返却する。Pool 外の Instance や未対応クラスの返却は warning を
出して拒否する。

Instance は既存の親子ツリーが `shared_ptr` を所有するため、Pool の内部 storage も
`shared_ptr` を使う。vector が再配置されても `Instance` 本体は移動せず、借用中の
Instance は active map で追跡する。再貸出し時は `Instance::init()`（BaseCube 系では
位置・基本物理プロパティの初期化）を実行する。
