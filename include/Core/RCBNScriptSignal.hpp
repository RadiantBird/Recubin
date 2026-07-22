#pragma once
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include "include/luau/lua.h"

class RCBNScriptConnection;

struct Listener {
    int  luaRef = LUA_NOREF;
    bool once   = false;
    int  id     = -1;
    std::string sourceLabel;
};

class RCBNScriptSignal : public std::enable_shared_from_this<RCBNScriptSignal> {
    lua_State*            m_mainL    = nullptr;
    std::vector<Listener> m_listeners;
    int                   m_nextId  = 0;

public:
    ~RCBNScriptSignal();

    int  connect(lua_State* L, int luaRef, bool once, std::string sourceLabel = "Unknown signal listener");
    void disconnect(int id);
    void disconnectAll();
    void fire(lua_State* L, std::function<int(lua_State*)> pushArgs = nullptr);
    void fire(std::function<int(lua_State*)> pushArgs = nullptr);
};

class RCBNScriptConnection {
    std::weak_ptr<RCBNScriptSignal> m_signal;
    int m_id;
public:
    RCBNScriptConnection(std::weak_ptr<RCBNScriptSignal> sig, int id)
        : m_signal(std::move(sig)), m_id(id) {}
    void disconnect();
};
