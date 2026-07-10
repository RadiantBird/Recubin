-- test_cyclic_module: 自分自身をrequireして循環検出をテストする
-- (モジュール本体は呼び出し元スクリプトのコルーチン上で実行されるため、
--  scriptグローバルは呼び出し元スクリプトを指す)
return require(script.CyclicModule)
