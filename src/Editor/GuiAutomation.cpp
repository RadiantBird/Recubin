#include <Editor/GuiAutomation.hpp>
#include <Util/PngWriter.hpp>
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
#include <thread>
#include <unordered_map>
#include <algorithm>

namespace {
struct Target { ImVec2 min{}, max{}; ImGuiID id=0; bool visible=false; std::uint64_t frame=0; };
enum class Kind { Move, Click, RightClick, Type, Key, Mouse, Down, Up, Wheel, Wait, Focus, Capture, Quit };
struct Action { Kind kind{}; std::string target,text,path; ImVec2 point{}; int button=0; ImGuiKey key=ImGuiKey_None; std::vector<ImGuiKey> modifiers; float x=0,y=0; std::uint64_t timeout=600,deadline=0; };
std::atomic_bool enabledFlag{false},quitFlag{false},started{false}; std::mutex mutex; std::deque<Action> queue; std::unordered_map<std::string,Target> current,published; Action active{}; bool activeValid=false,tapRelease=false,rightClickPressPending=false; std::uint64_t frame=0;
void report(bool ok,const std::string& s){(ok?std::cout:std::cerr)<<"[UIAUTO] "<<(ok?"OK ":"ERROR ")<<s<<'\n'<<std::flush;}
std::vector<std::string> tokenize(std::string_view line){std::vector<std::string> out;std::string s;bool q=false;for(size_t i=0;i<line.size();++i){char c=line[i];if(q&&c=='\\'&&i+1<line.size()&&(line[i+1]=='\\'||line[i+1]=='"')){s+=line[++i];continue;}if(c=='"'){q=!q;continue;}if(!q&&std::isspace((unsigned char)c)){if(!s.empty()){out.push_back(s);s.clear();}}else s+=c;}if(q)return {};if(!s.empty())out.push_back(s);return out;}
bool uintValue(std::string_view s,std::uint64_t& v){auto r=std::from_chars(s.data(),s.data()+s.size(),v);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
bool floatValue(std::string_view s,float& v){auto r=std::from_chars(s.data(),s.data()+s.size(),v);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();}
bool parseKey(std::string s,ImGuiKey& k){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::toupper(c);});if(s.size()==1&&s[0]>='A'&&s[0]<='Z'){k=(ImGuiKey)(ImGuiKey_A+s[0]-'A');return true;}if(s.size()==1&&s[0]>='0'&&s[0]<='9'){k=(ImGuiKey)(ImGuiKey_0+s[0]-'0');return true;}if(s=="ENTER")k=ImGuiKey_Enter;else if(s=="ESCAPE"||s=="ESC")k=ImGuiKey_Escape;else if(s=="TAB")k=ImGuiKey_Tab;else if(s=="BACKSPACE")k=ImGuiKey_Backspace;else if(s=="DELETE")k=ImGuiKey_Delete;else if(s=="LEFT")k=ImGuiKey_LeftArrow;else if(s=="RIGHT")k=ImGuiKey_RightArrow;else if(s=="UP")k=ImGuiKey_UpArrow;else if(s=="DOWN")k=ImGuiKey_DownArrow;else if(s.size()>1&&s[0]=='F'){std::uint64_t n;if(!uintValue(s.substr(1),n)||n<1||n>12)return false;k=(ImGuiKey)(ImGuiKey_F1+n-1);}else return false;return true;}
ImGuiKey modifier(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::toupper(c);});if(s=="CTRL"||s=="CONTROL")return ImGuiKey_LeftCtrl;if(s=="SHIFT")return ImGuiKey_LeftShift;if(s=="ALT")return ImGuiKey_LeftAlt;if(s=="SUPER"||s=="CMD")return ImGuiKey_LeftSuper;return ImGuiKey_None;}
bool pointFor(const std::string& n,ImVec2& p){auto i=published.find(n);if(i==published.end()||!i->second.visible)return false;p={(i->second.min.x+i->second.max.x)/2,(i->second.min.y+i->second.max.y)/2};return true;}
}
namespace GuiAutomation {
void configureFromArgs(int argc,char** argv){for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--ui-automation")enabledFlag=true;}
void start(){if(!enabledFlag||started.exchange(true))return;std::thread([]{std::string line;while(std::getline(std::cin,line)){if(line.empty())continue;auto t=tokenize(line);if(t.empty()){report(false,"invalid quoted command");continue;}Action a{};bool ok=true;if(t[0]=="help"){report(true,"help targets wait move click right_click type key mouse mouse_down mouse_up wheel focus_window capture quit");continue;}if(t[0]=="targets"){std::lock_guard l(mutex);std::vector<std::string> names;for(auto&[n,v]:published)if(v.visible)names.push_back(n);std::sort(names.begin(),names.end());std::string m="targets";for(auto&n:names){auto&v=published[n];m+=" "+n+"="+std::to_string(v.min.x)+","+std::to_string(v.min.y)+","+std::to_string(v.max.x)+","+std::to_string(v.max.y);}report(true,m);continue;}if(t[0]=="quit"&&t.size()==1)a.kind=Kind::Quit;else if(t[0]=="wait"&&t.size()>=2){a.kind=Kind::Wait;a.target=t[1];if(t.size()>2&&!uintValue(t[2],a.timeout))ok=false;}else if((t[0]=="move"||t[0]=="click"||t[0]=="right_click")&&t.size()==2){a.kind=t[0]=="move"?Kind::Move:(t[0]=="click"?Kind::Click:Kind::RightClick);a.target=t[1];}else if(t[0]=="type"&&t.size()>1){a.kind=Kind::Type;for(size_t i=1;i<t.size();++i){if(i>1)a.text+=' ';a.text+=t[i];}}else if(t[0]=="key"&&t.size()>=2){a.kind=Kind::Key;std::string spec=t[1];size_t at=0;while(at<spec.size()){size_t plus=spec.find('+',at);std::string part=spec.substr(at,plus==std::string::npos?std::string::npos:plus-at);if(plus==std::string::npos){if(!parseKey(part,a.key))ok=false;break;}ImGuiKey mod=modifier(part);if(mod==ImGuiKey_None)ok=false;else a.modifiers.push_back(mod);at=plus+1;}}else if(t[0]=="mouse"&&t.size()==3){a.kind=Kind::Mouse;ok=floatValue(t[1],a.x)&&floatValue(t[2],a.y);}else if((t[0]=="mouse_down"||t[0]=="mouse_up")&&t.size()==2){a.kind=t[0]=="mouse_down"?Kind::Down:Kind::Up;if(t[1]=="left")a.button=0;else if(t[1]=="right")a.button=1;else if(t[1]=="middle")a.button=2;else ok=false;}else if(t[0]=="wheel"&&t.size()==3){a.kind=Kind::Wheel;ok=floatValue(t[1],a.x)&&floatValue(t[2],a.y);}else if(t[0]=="focus_window"&&t.size()==2){a.kind=Kind::Focus;a.text=t[1];}else if(t[0]=="capture"&&t.size()==2){a.kind=Kind::Capture;a.path=t[1];}else ok=false;if(!ok){report(false,"malformed command");continue;}std::lock_guard l(mutex);queue.push_back(std::move(a));}}).detach();}
void beforeNewFrame() {
    if (!enabledFlag) return;
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
    if (action.kind == Kind::Wait) {
        if (action.deadline == 0) {
            queue.front().deadline = frame + queue.front().timeout;
            action.deadline = queue.front().deadline;
        }
        if (frame > action.deadline) {
            queue.pop_front();
            report(false, "wait target timeout: " + action.target);
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
            report(false, "target unavailable: " + action.target);
            return;
        }
        if (action.kind == Kind::Click) {
            const auto target = published.find(action.target);
            if (target == published.end() || target->second.id == 0) {
                report(false, "target unavailable: " + action.target);
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
    } else if (action.kind == Kind::Quit) {
        quitFlag = true;
        report(true, "quit");
    }
}
void afterNewFrame(){if(!enabledFlag)return;std::lock_guard l(mutex);current.clear();if(activeValid&&active.kind==Kind::Focus){ImGui::SetWindowFocus(active.text.c_str());report(true,"focus_window");activeValid=false;}}
void registerLastItem(std::string_view id){if(!enabledFlag)return;ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();current[std::string(id)]={a,b,ImGui::GetItemID(),ImGui::IsItemVisible()&&b.x>=a.x&&b.y>=a.y,frame};}
void afterRender(GLFWwindow* window){if(!enabledFlag)return;std::lock_guard l(mutex);published=current;if(activeValid&&active.kind==Kind::Capture&&window){int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);std::vector<std::uint8_t> p((size_t)std::max(w,0)*(size_t)std::max(h,0)*4);GLint f=0,b=0,a=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&f);glGetIntegerv(GL_READ_BUFFER,&b);glGetIntegerv(GL_PACK_ALIGNMENT,&a);glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,p.data());glBindFramebuffer(GL_READ_FRAMEBUFFER,f);glReadBuffer(b);glPixelStorei(GL_PACK_ALIGNMENT,a);std::string e;if(!PngWriter::writeRgba8(active.path,w,h,p,&e))report(false,e.empty()?"capture failed":e);else report(true,"capture "+active.path);activeValid=false;}if(quitFlag&&window)glfwSetWindowShouldClose(window,GLFW_TRUE);}
bool enabled(){return enabledFlag.load();}
}
