#pragma once
#include <Network/NetworkTypes.hpp>
#include <string>

namespace NetworkIdentity {
inline std::string userName(PeerId id) { return "User_" + std::to_string(id); }
inline std::string characterName(PeerId id) { return "PlayerCharacter_" + std::to_string(id); }
}
