// sokol_gfx の実装はこの翻訳単位にだけ置く。
// バックエンド（SOKOL_GLCORE 等）は CMake の compile definition で全ターゲット共通に選ぶ
// （sokol-shdc が生成するシェーダーヘッダーも同じ定義で分岐するため）。
// Windows の GL バックエンドは sokol 内蔵の GL ローダーを使う。GLEW をここで include すると
// gl* 関数ポインタの名前が衝突するので、この翻訳単位には GL/glew.h を入れないこと。
#define SOKOL_IMPL
#include <sokol/sokol_gfx.h>
