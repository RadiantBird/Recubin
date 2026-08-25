#include <Instances/UserInput.hpp>
#include <Core/IInputBackend.hpp>
#include <Core/InputKey.hpp>

// ── キー文字列テーブル ────────────────────────────────────────────
//  KeyCode / MouseButton と Luau 側へ渡す固定名(Roblox風)の対応表。
//  ポーリングと isPressed の双方で参照する。
namespace {

struct KeyEntry  { KeyCode     code; const char* name; };
struct MouseEntry{ MouseButton button; const char* name; };

const KeyEntry s_keyTable[] = {
    { KeyCode::A, "A" }, { KeyCode::B, "B" }, { KeyCode::C, "C" }, { KeyCode::D, "D" },
    { KeyCode::E, "E" }, { KeyCode::F, "F" }, { KeyCode::G, "G" }, { KeyCode::H, "H" },
    { KeyCode::I, "I" }, { KeyCode::J, "J" }, { KeyCode::K, "K" }, { KeyCode::L, "L" },
    { KeyCode::M, "M" }, { KeyCode::N, "N" }, { KeyCode::O, "O" }, { KeyCode::P, "P" },
    { KeyCode::Q, "Q" }, { KeyCode::R, "R" }, { KeyCode::S, "S" }, { KeyCode::T, "T" },
    { KeyCode::U, "U" }, { KeyCode::V, "V" }, { KeyCode::W, "W" }, { KeyCode::X, "X" },
    { KeyCode::Y, "Y" }, { KeyCode::Z, "Z" },
    { KeyCode::Num0, "0" }, { KeyCode::Num1, "1" }, { KeyCode::Num2, "2" },
    { KeyCode::Num3, "3" }, { KeyCode::Num4, "4" }, { KeyCode::Num5, "5" },
    { KeyCode::Num6, "6" }, { KeyCode::Num7, "7" }, { KeyCode::Num8, "8" },
    { KeyCode::Num9, "9" },
    { KeyCode::Up, "Up" }, { KeyCode::Down, "Down" }, { KeyCode::Left, "Left" }, { KeyCode::Right, "Right" },
    { KeyCode::Escape, "Escape" }, { KeyCode::Space, "Space" }, { KeyCode::Enter, "Return" },
    { KeyCode::Tab, "Tab" }, { KeyCode::Backspace, "Backspace" },
    { KeyCode::LeftShift, "LeftShift" }, { KeyCode::RightShift, "RightShift" },
    { KeyCode::LeftControl, "LeftControl" }, { KeyCode::RightControl, "RightControl" },
    { KeyCode::LeftAlt, "LeftAlt" }, { KeyCode::RightAlt, "RightAlt" },
    { KeyCode::F1, "F1" }, { KeyCode::F2, "F2" }, { KeyCode::F3, "F3" },
    { KeyCode::F4, "F4" }, { KeyCode::F5, "F5" }, { KeyCode::F6, "F6" },
    { KeyCode::F7, "F7" }, { KeyCode::F8, "F8" }, { KeyCode::F9, "F9" },
    { KeyCode::F10, "F10" }, { KeyCode::F11, "F11" }, { KeyCode::F12, "F12" },
};

const MouseEntry s_mouseTable[] = {
    { MouseButton::Left,   "MouseButton1" },
    { MouseButton::Right,  "MouseButton2" },
    { MouseButton::Middle, "MouseButton3" },
};

constexpr int KEY_COUNT   = static_cast<int>(sizeof(s_keyTable)   / sizeof(s_keyTable[0]));
constexpr int MOUSE_COUNT = static_cast<int>(sizeof(s_mouseTable) / sizeof(s_mouseTable[0]));

} // anonymous namespace

UserInput::UserInput()
    : Instance("UserInput")
    , Pressed(std::make_shared<RCBNScriptSignal>())
    , Released(std::make_shared<RCBNScriptSignal>())
    , m_prevKeyDown(KEY_COUNT, 0)
{
    for (int i = 0; i < MOUSE_COUNT; ++i) m_prevMouseDown[i] = 0;
}

bool UserInput::IsA(std::string className) {
    if (className == "UserInput") return true;
    return Instance::IsA(className);
}

std::shared_ptr<Instance> UserInput::clone() const {
    // UserInput は入力バックエンドを借用しているため複製しない
    return nullptr;
}

void UserInput::poll() {
    if (!m_input) return;

    // キーボード
    for (int i = 0; i < KEY_COUNT; ++i) {
        const bool down = m_input->isKeyDown(s_keyTable[i].code);
        const bool prev = m_prevKeyDown[i] != 0;
        if (down && !prev) {
            std::string name = s_keyTable[i].name;
            if (Pressed) Pressed->fire([name](lua_State* L) { lua_pushstring(L, name.c_str()); return 1; });
        } else if (!down && prev) {
            std::string name = s_keyTable[i].name;
            if (Released) Released->fire([name](lua_State* L) { lua_pushstring(L, name.c_str()); return 1; });
        }
        m_prevKeyDown[i] = down ? 1 : 0;
    }

    // マウスボタン
    for (int i = 0; i < MOUSE_COUNT; ++i) {
        const bool down = m_input->isMouseButtonDown(s_mouseTable[i].button);
        const bool prev = m_prevMouseDown[i] != 0;
        if (down && !prev) {
            std::string name = s_mouseTable[i].name;
            if (Pressed) Pressed->fire([name](lua_State* L) { lua_pushstring(L, name.c_str()); return 1; });
        } else if (!down && prev) {
            std::string name = s_mouseTable[i].name;
            if (Released) Released->fire([name](lua_State* L) { lua_pushstring(L, name.c_str()); return 1; });
        }
        m_prevMouseDown[i] = down ? 1 : 0;
    }
}

bool UserInput::isPressed(const std::string& key) const {
    if (!m_input) return false;
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (key == s_keyTable[i].name) return m_input->isKeyDown(s_keyTable[i].code);
    }
    for (int i = 0; i < MOUSE_COUNT; ++i) {
        if (key == s_mouseTable[i].name) return m_input->isMouseButtonDown(s_mouseTable[i].button);
    }
    return false;
}
