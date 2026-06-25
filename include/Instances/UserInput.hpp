#pragma once

#include <Instances/Instance.hpp>
#include <Core/RCBNScriptSignal.hpp>
#include <string>
#include <vector>
#include <cstdint>

class IInputBackend; // Forward declaration

// ==================================================================
//  UserInput  (User.Input)
//
//  Roblox の UserInputService 相当。毎フレーム入力を前フレームと比較し、
//  キー/マウスボタンの押下・解放を Pressed / Released シグナルで通知する。
//  キーは文字列("W","Space","MouseButton1"等)で表現する。
//  入力の読み取りは User が所有する IInputBackend を借用して行う。
// ==================================================================
class UserInput : public Instance {
public:
    std::shared_ptr<RCBNScriptSignal> Pressed;  // 引数: 押されたキー名(string)
    std::shared_ptr<RCBNScriptSignal> Released; // 引数: 離されたキー名(string)

    UserInput();

    std::string getClassName() override { return "UserInput"; }
    bool IsA(std::string className) override;
    std::shared_ptr<Instance> clone() const override;

    // 入力供給源(User が所有する IInputBackend)を借用する
    void setBackend(IInputBackend* input) { m_input = input; }

    // 毎フレーム呼び出し、前フレームとの差分で Pressed/Released を発火する
    void poll();

    // 指定キー/マウスボタン(文字列)が現在押されているか
    bool isPressed(const std::string& key) const;

private:
    IInputBackend* m_input = nullptr;  // 借用(所有しない)
    std::vector<uint8_t> m_prevKeyDown;     // キーボード(キーテーブルと同順)
    uint8_t              m_prevMouseDown[3]; // マウス(マウステーブルと同順)
};
