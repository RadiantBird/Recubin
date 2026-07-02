#pragma once
#include <include/Math/Vector3.hpp>

namespace Pathfinding {

// 経路上の1点で要求される移動アクション
enum class WaypointAction {
    Walk,
    Jump, // このウェイポイント地点でジャンプが必要（対応する着地点は次のウェイポイント）
};

struct PathWaypoint {
    Vector3 Position;
    WaypointAction Action = WaypointAction::Walk;
};

} // namespace Pathfinding
