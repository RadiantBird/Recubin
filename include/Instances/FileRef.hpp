#pragma once
#include <Instances/PhysicalFileInstance.hpp>

// FileRef — ファイル（アセット）への参照を表す軽量インスタンス。
// パスはエディタで設定され YAML（ContentPath キー）に保存されるため、
// Packager に追跡・同梱・書換される。スクリプトは生パスを書かず、この
// インスタンスを消費プロパティ（例: Sound.Source）へ参照として渡す。
// FileRef型はPhysicalFileInstance.hppの中央X-macro定義から生成される。
