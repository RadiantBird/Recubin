#include <Editor/GuiAutomation.hpp>
#include <Editor/GuiAutomationCommand.hpp>
#include <Util/Platform.hpp>
#include <Util/IPlatform.hpp>
#include <Util/PngWriter.hpp>
#include <Util/ApiProfiler.hpp>
#include <Util/FrameProfiler.hpp>
#include <include/imgui/imgui.h>
#include <include/imgui/imgui_internal.h>
#include <include/GL/glew.h>
#include <include/GLFW/glfw3.h>
#include <atomic>
#include <charconv>
#include <cctype>
#include <deque>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <algorithm>

namespace {
struct Target { ImVec2 min{}, max{}; ImGuiID id=0; bool visible=false; std::uint64_t frame=0; };
enum class Kind { Move, Click, RightClick, Type, Key, Mouse, Down, Up, Wheel, Wait, Focus, Capture, Quit, State, Profile, ScriptProfile, WaitFrames };
struct Action { Kind kind{}; std::string target,text,path; ImVec2 point{}; int button=0; ImGuiKey key=ImGuiKey_None; std::vector<ImGuiKey> modifiers; float x=0,y=0; std::uint64_t timeout=600,deadline=0; };
std::atomic_bool enabledFlag{false},hiddenFlag{false},quitFlag{false},started{false}; std::mutex mutex; std::deque<Action> queue; std::unordered_map<std::string,Target> current,published; Action active{}; bool activeValid=false,tapRelease=false,rightClickPressPending=false; std::uint64_t frame=0;
void report(bool ok,const std::string& s){(ok?std::cout:std::cerr)<<"[UIAUTO] "<<(ok?"OK ":"ERROR ")<<s<<'\n'<<std::flush;}
bool uintValue(std::string_view s,std::uint64_t& v){auto r=std::from_chars(s.data(),s.data()+s.size(),v);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
bool floatValue(std::string_view s,float& v){auto r=std::from_chars(s.data(),s.data()+s.size(),v);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
bool parseKey(std::string s,ImGuiKey& k){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::toupper(c);});if(s.size()==1&&s[0]>='A'&&s[0]<='Z'){k=(ImGuiKey)(ImGuiKey_A+s[0]-'A');return true;}if(s.size()==1&&s[0]>='0'&&s[0]<='9'){k=(ImGuiKey)(ImGuiKey_0+s[0]-'0');return true;}if(s=="ENTER")k=ImGuiKey_Enter;else if(s=="ESCAPE"||s=="ESC")k=ImGuiKey_Escape;else if(s=="TAB")k=ImGuiKey_Tab;else if(s=="BACKSPACE")k=ImGuiKey_Backspace;else if(s=="DELETE")k=ImGuiKey_Delete;else if(s=="LEFT")k=ImGuiKey_LeftArrow;else if(s=="RIGHT")k=ImGuiKey_RightArrow;else if(s=="UP")k=ImGuiKey_UpArrow;else if(s=="DOWN")k=ImGuiKey_DownArrow;else if(s.size()>1&&s[0]=='F'){std::uint64_t n;if(!uintValue(s.substr(1),n)||n<1||n>12)return false;k=(ImGuiKey)(ImGuiKey_F1+n-1);}else return false;return true;}
ImGuiKey modifier(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::toupper(c);});if(s=="CTRL"||s=="CONTROL")return ImGuiKey_LeftCtrl;if(s=="SHIFT")return ImGuiKey_LeftShift;if(s=="ALT")return ImGuiKey_LeftAlt;if(s=="SUPER"||s=="CMD")return ImGuiKey_LeftSuper;return ImGuiKey_None;}

std::string jsonEscape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out;
}
std::string jsonString(std::string_view text) { return "\"" + jsonEscape(text) + "\""; }
std::string windowLabel(const ImGuiWindow* window) {
    if (!window) return "null";
    // 表示名は翻訳されるため、"###"以降の安定したIDがあればそれを使う。
    const std::string_view name = window->Name;
    const size_t idStart = name.find("###");
    return jsonString(idStart == std::string_view::npos ? name : name.substr(idStart));
}
// 直前のフレームが終わった時点のImGui状態を1行のJSONにする。ウィンドウや画像を見なくても、
// 「どのポップアップが開いているか」「フォーカスはどこか」「どのtargetが見えているか」を取得できる。
// 呼び出し側がmutexを保持していること(publishedを読むため)。
std::string stateJson() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context) return "{}";
    const ImGuiContext& g = *context;
    constexpr size_t MAX_LISTED = 60;

    std::string popups;
    for (int i = 0; i < g.OpenPopupStack.Size; ++i) {
        if (i > 0) popups += ',';
        popups += windowLabel(g.OpenPopupStack[i].Window);
    }
    std::string windows;
    size_t windowCount = 0;
    for (const ImGuiWindow* window : g.Windows) {
        if (!window || !window->Active || window->Hidden) continue;
        if (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip)) continue;
        if (windowCount++ < MAX_LISTED) windows += (windows.empty() ? "" : ",") + windowLabel(window);
    }
    std::vector<std::string> names;
    for (const auto& [name, target] : published) if (target.visible) names.push_back(name);
    std::sort(names.begin(), names.end());
    std::string targets;
    for (size_t i = 0; i < names.size() && i < MAX_LISTED; ++i)
        targets += (targets.empty() ? "" : ",") + jsonString(names[i]);

    std::string json = "{\"frame\":" + std::to_string(frame);
    json += std::string(",\"hiddenWindow\":") + (hiddenFlag.load() ? "true" : "false");
    json += ",\"popups\":[" + popups + "]";
    json += ",\"modal\":" + windowLabel(ImGui::GetTopMostPopupModal());
    json += ",\"focus\":" + windowLabel(g.NavWindow);
    json += ",\"hovered\":" + windowLabel(g.HoveredWindow);
    json += ",\"windows\":[" + windows + "]";
    json += ",\"targetCount\":" + std::to_string(names.size());
    json += ",\"targets\":[" + targets + "]}";
    return json;
}

// FrameProfilerの全区間・カウンター・GPU時間を1行のJSONにする(直近240フレームの平均/最大)。
std::string profileJson() {
    FrameProfiler& profiler = FrameProfiler::get();
    char buffer[160];
    std::string json = "{";

    FrameProfiler::FrameSnapshot frame;
    if (profiler.getFrameSnapshot(frame)) {
        std::snprintf(buffer, sizeof(buffer),
                      "\"fps\":{\"current\":%.1f,\"average\":%.1f},\"frameMs\":{\"current\":%.2f,\"average\":%.2f},\"frames\":%zu",
                      frame.latestFps, frame.averageFps, frame.latestFrameMs, frame.averageFrameMs, frame.count);
        json += buffer;
    }

    json += ",\"sectionsMs\":{";
    bool first = true;
    for (const std::string& name : profiler.sectionNames()) {
        FrameProfiler::SectionSnapshot snapshot;
        if (!profiler.getSectionSnapshot(name.c_str(), snapshot)) continue;
        std::snprintf(buffer, sizeof(buffer), "{\"avg\":%.3f,\"max\":%.3f}", snapshot.averageMs, snapshot.peakMs);
        json += std::string(first ? "" : ",") + jsonString(name) + ":" + buffer;
        first = false;
    }
    json += "},\"gpuMs\":{";
    first = true;
    for (const char* name : {"gpuTotal", "gpuShadow", "gpuMain", "gpuSurfaceMarks", "gpuExtras"}) {
        FrameProfiler::GpuSnapshot snapshot;
        if (!profiler.getGpuSnapshot(name, snapshot) || snapshot.count == 0) continue;
        std::snprintf(buffer, sizeof(buffer), "{\"avg\":%.3f,\"max\":%.3f}", snapshot.averageMs, snapshot.peakMs);
        json += std::string(first ? "" : ",") + jsonString(name) + ":" + buffer;
        first = false;
    }
    json += "},\"counters\":{";
    first = true;
    for (const std::string& name : profiler.counterNames()) {
        FrameProfiler::CounterSnapshot snapshot;
        if (!profiler.getCounterSnapshot(name.c_str(), snapshot)) continue;
        std::snprintf(buffer, sizeof(buffer), "{\"avg\":%.1f,\"max\":%lld}", snapshot.average, snapshot.peak);
        json += std::string(first ? "" : ",") + jsonString(name) + ":" + buffer;
        first = false;
    }
    json += "}}";
    return json;
}
bool pointFor(const std::string& n,ImVec2& p){auto i=published.find(n);if(i==published.end()||!i->second.visible)return false;p={(i->second.min.x+i->second.max.x)/2,(i->second.min.y+i->second.max.y)/2};return true;}
bool parseCommand(std::string_view line, Action& parsedAction,
                  std::string& immediateCommand) {
    const auto parsed = parseGuiAutomationCommand(line);
    if (!parsed) return false;
    std::vector<std::string> t;
    t.reserve(parsed->arguments.size() + 1);
    t.push_back(parsed->name);
    t.insert(t.end(), parsed->arguments.begin(), parsed->arguments.end());
    immediateCommand.clear();
    if (t[0] == "help" && t.size() == 1) { immediateCommand = "help"; return true; }
    if (t[0] == "targets" && t.size() == 1) {
        immediateCommand = "targets"; return true;
    }
    Action action{}; bool ok = true;
    if (t[0] == "quit" && t.size() == 1) action.kind = Kind::Quit;
    else if (t[0] == "state" && t.size() == 1) action.kind = Kind::State;
    else if (t[0] == "profile" && t.size() == 1) action.kind = Kind::Profile;
    else if (t[0] == "script_profile" && t.size() == 2) { action.kind = Kind::ScriptProfile; action.text = t[1]; }
    else if (t[0] == "wait_frames" && t.size() == 2) {
        action.kind = Kind::WaitFrames;
        ok = uintValue(t[1], action.timeout);  // timeoutフィールドを待つフレーム数に流用
    }
    else if (t[0] == "wait" && (t.size() == 2 || t.size() == 3)) {
        action.kind = Kind::Wait; action.target = t[1];
        if (t.size() == 3) ok = uintValue(t[2], action.timeout);
    } else if ((t[0] == "move" || t[0] == "click" || t[0] == "right_click") && t.size() == 2) {
        action.kind = t[0] == "move" ? Kind::Move : (t[0] == "click" ? Kind::Click : Kind::RightClick); action.target = t[1];
    } else if (t[0] == "type" && t.size() > 1) {
        action.kind = Kind::Type; for (size_t i = 1; i < t.size(); ++i) { if (i > 1) action.text += ' '; action.text += t[i]; }
    } else if (t[0] == "key" && t.size() == 2) {
        action.kind = Kind::Key; const std::string& spec = t[1]; size_t begin = 0;
        while (begin < spec.size()) {
            const size_t plus = spec.find('+', begin); const std::string part = spec.substr(begin, plus == std::string::npos ? std::string::npos : plus - begin);
            if (part.empty()) { ok = false; break; }
            if (plus == std::string::npos) { ok = parseKey(part, action.key); break; }
            const ImGuiKey mod = modifier(part); if (mod == ImGuiKey_None) { ok = false; break; }
            action.modifiers.push_back(mod); begin = plus + 1;
        }
        if (begin == spec.size()) ok = false;
    } else if (t[0] == "mouse" && t.size() == 3) { action.kind = Kind::Mouse; ok = floatValue(t[1], action.x) && floatValue(t[2], action.y); }
    else if ((t[0] == "mouse_down" || t[0] == "mouse_up") && t.size() == 2) {
        action.kind = t[0] == "mouse_down" ? Kind::Down : Kind::Up;
        if (t[1] == "left") action.button = 0; else if (t[1] == "right") action.button = 1; else if (t[1] == "middle") action.button = 2; else ok = false;
    } else if (t[0] == "wheel" && t.size() == 3) { action.kind = Kind::Wheel; ok = floatValue(t[1], action.x) && floatValue(t[2], action.y); }
    else if (t[0] == "focus_window" && t.size() == 2) { action.kind = Kind::Focus; action.text = t[1]; }
    else if (t[0] == "capture" && t.size() == 2) { action.kind = Kind::Capture; action.path = t[1]; }
    else ok = false;
    if (!ok) return false;
    parsedAction = std::move(action);
    return true;
}

bool enqueueCommand(std::string_view line) {
    if (!validateGuiAutomationCommand(line)) {
        report(false, "malformed command");
        return false;
    }
    Action action{}; std::string immediateCommand;
    if (!parseCommand(line, action, immediateCommand)) {
        report(false, "malformed command");
        return false;
    }
    if (immediateCommand == "help") {
        report(true, "help targets state profile script_profile wait_frames wait move click right_click type key mouse mouse_down mouse_up wheel focus_window capture quit");
        return true;
    }
    if (immediateCommand == "targets") {
        std::lock_guard lock(mutex); std::string message = "targets";
        for (const auto& [name, target] : published) if (target.visible)
            message += " " + name + "=" + std::to_string(target.min.x) + "," + std::to_string(target.min.y) + "," + std::to_string(target.max.x) + "," + std::to_string(target.max.y);
        report(true, message);
        return true;
    }
    std::lock_guard lock(mutex); queue.push_back(std::move(action));
    return true;
}
}
namespace GuiAutomation {
void configureFromArgs(int argc,char** argv){for(int i=1;i<argc;++i){const std::string_view argument(argv[i]);if(argument=="--ui-automation")enabledFlag=true;else if(argument=="--ui-automation-hidden")hiddenFlag=true;}if(!enabledFlag)hiddenFlag=false;}
bool hiddenWindow(){return hiddenFlag.load();}
void start(){ if (enabledFlag) started = true; }
void beforeNewFrame() {
    if (!enabledFlag) return;
    if (started) {
        for (int count = 0; count < 32; ++count) {
            auto line = getPlatform().pollStdinLine();
            if (!line) break;
            if (!line->empty()) enqueueCommand(*line);
        }
    }
    ImGui::GetIO().AddFocusEvent(true);
    ++frame;
    std::lock_guard lock(mutex);
    if (tapRelease) {
        if (active.kind == Kind::Key) {
            ImGui::GetIO().AddKeyEvent(active.key, false);
            for (auto it = active.modifiers.rbegin(); it != active.modifiers.rend(); ++it)
                ImGui::GetIO().AddKeyEvent(*it, false);
            report(true, "key");
        } else {
            ImGui::GetIO().AddMousePosEvent(active.point.x, active.point.y);
            ImGui::GetIO().AddMouseButtonEvent(active.button, false);
            report(true, active.kind == Kind::Click ? "click " + active.target : "right_click " + active.target);
        }
        tapRelease = false;
        activeValid = false;
        return;
    }
    if (rightClickPressPending) {
        ImGui::GetIO().AddMousePosEvent(active.point.x, active.point.y);
        ImGui::GetIO().AddMouseButtonEvent(1, true);
        rightClickPressPending = false;
        tapRelease = true;
        return;
    }
    if (activeValid || queue.empty()) return;
    Action action = queue.front();
    if (action.kind == Kind::WaitFrames) {
        if (action.deadline == 0) {
            queue.front().deadline = frame + std::max<std::uint64_t>(action.timeout, 1);
            return;
        }
        if (frame < action.deadline) return;
        queue.pop_front();
        report(true, "wait_frames " + std::to_string(action.timeout));
        return;
    }
    if (action.kind == Kind::Wait) {
        if (action.deadline == 0) {
            queue.front().deadline = frame + queue.front().timeout;
            action.deadline = queue.front().deadline;
        }
        if (frame > action.deadline) {
            queue.pop_front();
            report(false, "wait target timeout: " + action.target + " state=" + stateJson());
        } else if (!pointFor(action.target, action.point)) {
            return;
        } else {
            queue.pop_front();
            report(true, "wait target " + action.target);
        }
    } else {
        queue.pop_front();
    }
    if (action.kind == Kind::Move || action.kind == Kind::Click || action.kind == Kind::RightClick) {
        if (!pointFor(action.target, action.point)) {
            report(false, "target unavailable: " + action.target + " state=" + stateJson());
            return;
        }
        if (action.kind == Kind::Click) {
            const auto target = published.find(action.target);
            if (target == published.end() || target->second.id == 0) {
                report(false, "target unavailable: " + action.target + " state=" + stateJson());
                return;
            }
            ImGui::ActivateItemByID(target->second.id);
            report(true, "click " + action.target);
            return;
        }
        ImGui::GetIO().AddMousePosEvent(action.point.x, action.point.y);
        if (action.kind != Kind::Move) {
            action.button = action.kind == Kind::Click ? 0 : 1;
            active = action;
            activeValid = true;
            if (action.kind == Kind::RightClick) {
                rightClickPressPending = true;
            } else {
                ImGui::GetIO().AddMouseButtonEvent(action.button, true);
                tapRelease = true;
            }
        } else {
            report(true, "move " + action.target);
        }
    } else if (action.kind == Kind::Mouse) {
        ImGui::GetIO().AddMousePosEvent(action.x, action.y);
        report(true, "mouse");
    } else if (action.kind == Kind::Down) {
        ImGui::GetIO().AddMouseButtonEvent(action.button, true);
        report(true, "mouse_down");
    } else if (action.kind == Kind::Up) {
        ImGui::GetIO().AddMouseButtonEvent(action.button, false);
        report(true, "mouse_up");
    } else if (action.kind == Kind::Wheel) {
        ImGui::GetIO().AddMouseWheelEvent(action.x, action.y);
        report(true, "wheel");
    } else if (action.kind == Kind::Type) {
        ImGui::GetIO().AddInputCharactersUTF8(action.text.c_str());
        report(true, "type");
    } else if (action.kind == Kind::Key) {
        for (auto key : action.modifiers) ImGui::GetIO().AddKeyEvent(key, true);
        ImGui::GetIO().AddKeyEvent(action.key, true);
        active = action;
        activeValid = true;
        tapRelease = true;
    } else if (action.kind == Kind::Focus || action.kind == Kind::Capture) {
        active = action;
        activeValid = true;
    } else if (action.kind == Kind::State) {
        report(true, "state " + stateJson());
    } else if (action.kind == Kind::Profile) {
        report(true, "profile " + profileJson());
    } else if (action.kind == Kind::ScriptProfile) {
        if (action.text == "on") { ApiProfiler::setEnabled(true); report(true, "script_profile on"); }
        else if (action.text == "off") { ApiProfiler::setEnabled(false); report(true, "script_profile off"); }
        else if (action.text == "reset") { ApiProfiler::reset(); report(true, "script_profile reset"); }
        else report(true, "script_profile " + ApiProfiler::toJson());
    } else if (action.kind == Kind::Quit) {
        quitFlag.exchange(true);
        report(true, "quit");
    }
}
void afterNewFrame(){if(!enabledFlag)return;std::lock_guard l(mutex);current.clear();if(activeValid&&active.kind==Kind::Focus){ImGui::SetWindowFocus(active.text.c_str());report(true,"focus_window");activeValid=false;}}
void registerLastItem(std::string_view id){if(!enabledFlag)return;ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();current[std::string(id)]={a,b,ImGui::GetItemID(),ImGui::IsItemVisible()&&b.x>=a.x&&b.y>=a.y,frame};}
void afterRender(GLFWwindow* window){if(!enabledFlag)return;std::lock_guard l(mutex);published=current;if(activeValid&&active.kind==Kind::Capture&&window){int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);std::vector<std::uint8_t> p((size_t)std::max(w,0)*(size_t)std::max(h,0)*4);GLint f=0,b=0,a=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&f);glGetIntegerv(GL_READ_BUFFER,&b);glGetIntegerv(GL_PACK_ALIGNMENT,&a);glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,p.data());glBindFramebuffer(GL_READ_FRAMEBUFFER,f);glReadBuffer(b);glPixelStorei(GL_PACK_ALIGNMENT,a);std::string e;if(!PngWriter::writeRgba8(active.path,w,h,p,&e))report(false,e.empty()?"capture failed":e);else report(true,"capture "+active.path);activeValid=false;}if(quitFlag.exchange(false)&&window)glfwSetWindowShouldClose(window,GLFW_TRUE);}
bool enabled(){return enabledFlag.load();}
}
