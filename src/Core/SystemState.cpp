#include <Core/SystemState.hpp>

SystemState& SystemState::get() {
    static SystemState instance;
    return instance;
}
