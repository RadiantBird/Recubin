#include <Core/RCBNScriptSignal.hpp>
#include <Core/LuauEngine.hpp>
#include <Util/Logger.hpp>
#include <iostream>

RCBNScriptSignal::~RCBNScriptSignal() {
    if (!m_mainL) return;
    for (auto& l : m_listeners) {
        if (l.luaRef != LUA_NOREF)
            lua_unref(m_mainL, l.luaRef);
    }
    m_listeners.clear();
}

int RCBNScriptSignal::connect(lua_State* L, int luaRef, bool once, std::string sourceLabel) {
    // Lが個々のScriptのコルーチンスレッドの場合、そのScriptの実行終了とともにLuau側で
    // 解放/GCされうる(ループを持たない単発スクリプトで顕著)。m_mainLはSignal自身の寿命
    // (Systemと同程度に長い)だけ生存する必要があるため、常にVMのメインスレッドを指すよう
    // lua_mainthread()で解決してからキャッシュする(そうしないと、そのコルーチンが破棄された後の
    // disconnect/fireでdangling lua_State*を使ってlua_unref等がクラッシュする)
    if (!m_mainL) m_mainL = lua_mainthread(L);
    int id = m_nextId++;
    if (sourceLabel.empty()) sourceLabel = "Unknown signal listener";
    m_listeners.push_back({ luaRef, once, id, std::move(sourceLabel) });
    return id;
}

void RCBNScriptSignal::disconnect(int id) {
    for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        if (it->id == id) {
            if (m_mainL && it->luaRef != LUA_NOREF)
                lua_unref(m_mainL, it->luaRef);
            m_listeners.erase(it);
            return;
        }
    }
}

void RCBNScriptSignal::disconnectAll() {
    if (m_mainL) {
        for (auto& l : m_listeners) {
            if (l.luaRef != LUA_NOREF)
                lua_unref(m_mainL, l.luaRef);
        }
    }
    m_listeners.clear();
}

void RCBNScriptSignal::fire(lua_State* L, std::function<int(lua_State*)> pushArgs) {
    // コピーしてイテレート（コールバック内で disconnect されても安全）
    auto copy = m_listeners;
    for (auto& l : copy) {
        if (l.luaRef == LUA_NOREF) continue;
        lua_State* mainL = lua_mainthread(L);
        lua_State* callbackCo = lua_newthread(L);
        int callbackRef = lua_ref(L, -1);
        lua_pop(L, 1);

        lua_callbacks(callbackCo)->userdata = lua_callbacks(mainL)->userdata;
        auto* engine = static_cast<LuauEngine*>(lua_callbacks(callbackCo)->userdata);
        std::chrono::steady_clock::time_point previousStart{};
        if (engine) {
            previousStart = engine->beginSignalCallback();
            engine->beginProtectedExecution();
        }

        lua_rawgeti(callbackCo, LUA_REGISTRYINDEX, l.luaRef);
        int nargs = pushArgs ? pushArgs(callbackCo) : 0;
        int status = lua_resume(callbackCo, L, nargs);
        const std::string context = "Signal | " + l.sourceLabel;
        if (status == LUA_YIELD) {
            // PathfindingService::FindPathだけはLuauEngineがコルーチンを保持し、
            // ナビメッシュ完成時に再開する。それ以外のSignal yieldは従来どおり未対応。
            if (engine && engine->isPathfindingCoroutine(callbackCo)) {
                // callbackRefはここで解放してよい。非同期要求側が独立した参照を保持する。
            } else if (engine) {
                engine->reportProtectedMessage(context, "Signal callback yielded; yielding listeners are not supported");
            } else {
                const std::string output = "[" + context + "] Signal callback yielded; yielding listeners are not supported";
                std::cerr << output << "\n";
                if (g_luauLogHook) g_luauLogHook("[ERROR] " + output);
            }
        } else if (status != 0) {
            if (engine) {
                engine->reportProtectedError(callbackCo, context);
            } else {
                const char* raw = lua_tostring(callbackCo, -1);
                const std::string output = "[" + context + "] " + (raw ? raw : "unknown error");
                std::cerr << output << "\n";
                if (g_luauLogHook) g_luauLogHook("[ERROR] " + output);
            }
        }
        if (engine) engine->endSignalCallback(previousStart);
        lua_unref(mainL, callbackRef);
        if (l.once) disconnect(l.id);
    }
}

void RCBNScriptSignal::fire(std::function<int(lua_State*)> pushArgs) {
    if (m_mainL) {
        fire(m_mainL, pushArgs);
    }
}

void RCBNScriptConnection::disconnect() {
    if (auto sig = m_signal.lock()) {
        sig->disconnect(m_id);
        m_signal.reset();
    }
}
