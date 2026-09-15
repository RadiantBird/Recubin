#include <Core/User.hpp>
#include <Core/CharacterRig.hpp>
#include <Instances/System.hpp>
#include <Core/NullInputBackend.hpp>
#include <Network/NetworkIdentity.hpp>
#include <Instances/StarterCharacter.hpp>
#include <Instances/SpawnLocation.hpp>
#include <Instances/Workspace.hpp>
#include <Instances/Animation.hpp>
#include <Instances/Weld.hpp>
#include <include/Util/Logger.hpp>
#include <include/Core/Physics.hpp>
#include <include/Core/LuauEngine.hpp>
#include <include/Core/PropertyRegistry.hpp>
#include <algorithm>

User* User::s_instance = nullptr;

namespace {
constexpr float DEFAULT_CHARACTER_SMOOTHING = 0.15f;

float sanitizeCharacterSmoothing(float value) {
    if (!std::isfinite(value)) return DEFAULT_CHARACTER_SMOOTHING;
    return std::clamp(value, 0.0f, 1.0f);
}

const bool s_userRegistered = [] {
    using namespace PropertyRegistry;
    PropertyDesc controlMode = custom("ControlMode", PropType::Enum,
        [](Instance* object) {
            return PropValue(static_cast<int>(static_cast<User*>(object)->getControlMode()));
        },
        [](Instance* object, const PropValue& value) {
            static_cast<User*>(object)->setControlMode(
                static_cast<User::ControlMode>(std::get<int>(value)));
        });
    controlMode.enumNames = {{"Free", 0}, {"Character", 1}, {"Program", 2}};
    controlMode.yamlEnumAsString = true;
    PropertyDesc smoothing = custom("CharacterSmoothing", PropType::Float,
        [](Instance* object) { return PropValue(static_cast<User*>(object)->characterSmoothing); },
        [](Instance* object, const PropValue& value) {
            static_cast<User*>(object)->characterSmoothing =
                sanitizeCharacterSmoothing(std::get<float>(value));
        });
    smoothing.lo = 0.0f; smoothing.hi = 1.0f; smoothing.step = 0.01f;
    registerClass("User", {
        controlMode,
        field<&User::speed>("Speed", 0.0f, 10.0f, 0.01f),
        field<&User::rotationSpeed>("RotationSpeed", 0.0f, 10.0f, 0.01f),
        field<&User::mouseRotationSpeed>("MouseRotationSpeed", 0.0f, 2.0f, 0.01f),
        smoothing,
        field<&User::cameraDistance>("CameraDistance", 1.0f, 50.0f, 0.1f),
        field<&User::zoomSpeed>("ZoomSpeed", 0.0f, 1.0f, 0.01f),
        field<&User::mouseZoomSpeed>("MouseZoomSpeed", 0.0f, 10.0f, 0.1f),
        field<&User::gizmoSize>("GizmoSize", 0.05f, 0.50f, 0.01f).noYaml().luaReadOnly(),
        custom("MovementInputEnabled", PropType::Bool,
            [](Instance* object) { return PropValue(static_cast<User*>(object)->isMovementInputEnabled()); },
            [](Instance* object, const PropValue& value) {
                static_cast<User*>(object)->setMovementInputEnabled(std::get<bool>(value));
            }),
        custom("CameraInputEnabled", PropType::Bool,
            [](Instance* object) { return PropValue(static_cast<User*>(object)->isCameraInputEnabled()); },
            [](Instance* object, const PropValue& value) {
                static_cast<User*>(object)->setCameraInputEnabled(std::get<bool>(value));
            }),
        custom("HotkeyInputEnabled", PropType::Bool,
            [](Instance* object) { return PropValue(static_cast<User*>(object)->isHotkeyInputEnabled()); },
            [](Instance* object, const PropValue& value) {
                static_cast<User*>(object)->setHotkeyInputEnabled(std::get<bool>(value));
            }),
        custom("ToolInputEnabled", PropType::Bool,
            [](Instance* object) { return PropValue(static_cast<User*>(object)->isToolInputEnabled()); },
            [](Instance* object, const PropValue& value) {
                static_cast<User*>(object)->setToolInputEnabled(std::get<bool>(value));
            }),
    });
    return true;
}();
}
void User::setCursorType(CursorType type) {
    const auto value = static_cast<int>(type);
    m_cursorType = (value >= 0 && value <= 10) ? type : CursorType::Default;
}

const User::CursorImageSlot& User::getCursorImageSlot(std::size_t index) const {
    static const CursorImageSlot empty{};
    return index < m_cursorImages.size() ? m_cursorImages[index] : empty;
}

void User::setCursorImagePath(std::size_t index, const std::string& path) {
    if (index < m_cursorImages.size()) m_cursorImages[index].contentPath = path;
}

void User::setCursorHotspotX(std::size_t index, int value) {
    if (index < m_cursorImages.size()) m_cursorImages[index].hotspotX = std::max(0, value);
}

void User::setCursorHotspotY(std::size_t index, int value) {
    if (index < m_cursorImages.size()) m_cursorImages[index].hotspotY = std::max(0, value);
}

void User::setCursorSize(std::size_t index, int value) {
    if (index < m_cursorImages.size())
        m_cursorImages[index].size = std::clamp(value, 1, MAX_CURSOR_SIZE);
}

bool User::applyCursor(bool gameplayHovered, float contentScale) {
    if (!m_input) return false;
    if (m_mouseLockEnabled || isRightMouseRotating || m_externalDragActive) {
        m_input->setMouseCaptured(true);
        return false;
    }
    if (!gameplayHovered || m_cursorType == CursorType::Default) return false;
    const auto index = static_cast<std::size_t>(m_cursorType) - 1;
    const auto& slot = m_cursorImages[index];
    if (slot.contentPath.empty()) return false;
    const CursorImageData* image = m_cursorImageProcessor.prepare(
        index, slot.contentPath, slot.hotspotX, slot.hotspotY, slot.size, contentScale);
    return image && m_input->setCustomCursor(*image);
}

User::User(std::unique_ptr<IInputBackend> input, bool isRemoteUser)
    : Instance("User"),
      m_input(std::move(input)),
      cam(current_camera),
      cpos(current_camera.Position),
      forward(0, 0, -1),
      right(1, 0, 0),
      up(0, 1, 0),
      lastFKeyPressed(false),
      isRemoteUser(isRemoteUser)
{
    if (!isRemoteUser) s_instance = this;
    updateVectors();

    // User.Input を生成し、入力供給源を借用させる
    Input = std::make_shared<UserInput>();
    Input->Name = "Input";
    Input->setBackend(m_input.get());

    CharacterAdded = std::make_shared<RCBNScriptSignal>();
    ExitRequested = std::make_shared<RCBNScriptSignal>();
}

std::shared_ptr<User> User::createRemoteUser(uint32_t peerId) {
    auto user = std::make_shared<User>(std::make_unique<NullInputBackend>(), true);
    user->Name = NetworkIdentity::userName(peerId);
    user->peerId = peerId;
    user->lockRuntimeName();
    user->initializeInventory();
    return user;
}

bool User::applyNetworkIdentity(uint32_t newPeerId) {
    if (newPeerId == 0) return false;
    const std::string oldName = Name;
    if (!renameToAuthoritative(NetworkIdentity::userName(newPeerId))) return false;
    if (character && !character->renameToAuthoritative(NetworkIdentity::characterName(newPeerId))) {
        renameToAuthoritative(oldName);
        return false;
    }
    peerId = newPeerId;
    lockRuntimeName();
    if (character) character->lockRuntimeName();
    RCBN_LOG("User: applied authoritative identity " << Name
             << (character ? " / " + character->Name : ""));
    return true;
}

void User::initializeInventory() {
    // Inventory を User の子として追加（コンストラクタ後に呼ぶ）
    if (Inventory) {
        Inventory->Name = "Inventory";
        // すでに Parent が設定されていなければ addChild
        if (!Inventory->Parent.lock()) {
            this->addChild(Inventory);
        }
    }
}

void User::resetToolState() {
    if (m_toolWeld) {
        if (auto parent = m_toolWeld->Parent.lock()) parent->removeChild(m_toolWeld->Name);
        m_toolWeld.reset();
    }
    for (auto& slot : Slots) slot = nullptr;
    currentTool      = nullptr;
    currentSlotIndex = -1;
}

void User::resetInventory() {
    // 先に強参照を切り、旧 Inventory 以下の Tool がスロット経由で生存しないようにする。
    resetToolState();
    if (Inventory) {
        if (auto parent = Inventory->Parent.lock()) {
            parent->removeChild(Inventory->Name);
        }
    }
    Inventory = std::make_shared<Folder>();
    Inventory->Name = "Inventory";
}

void User::syncToolsFromInventory() {
    resetToolState();
    if (!Inventory) return;

    for (const auto& [name, child] : Inventory->children) {
        (void)name;
        auto tool = std::dynamic_pointer_cast<Tool>(child);
        if (!tool) continue;

        if (addToolToSlot(tool) < 0) {
            RCBN_WARN("User::syncToolsFromInventory: hotbar is full; ignoring Tool " + tool->Name);
        }
    }
}

int User::addToolToSlot(std::shared_ptr<Tool> tool, int slotIndex) {
    if (!tool) return -1;

    // SceneLoader が新Inventoryを user->Inventory に採用する前にToolをツリーへ
    // 接続する場合があるため、親変更通知だけに依存せず、同期時にも所有Userを記録する。
    tool->m_inventoryOwner =
        std::static_pointer_cast<User>(shared_from_this());

    // Inventory への再収納や、スクリプトからの重複追加で同じ Tool が
    // 複数スロットに入らないよう、既存スロットをそのまま返す。
    for (int i = 0; i < static_cast<int>(Slots.size()); ++i) {
        if (Slots[i] == tool) return i;
    }

    // スロット番号の決定（負なら先頭の空きを探す）
    if (slotIndex < 0) {
        slotIndex = -1;
        for (int i = 0; i < static_cast<int>(Slots.size()); ++i) {
            if (!Slots[i]) { slotIndex = i; break; }
        }
        if (slotIndex < 0) return -1; // 空きなし
    } else if (slotIndex >= static_cast<int>(Slots.size())) {
        return -1; // 範囲外
    }

    // まだ Inventory 配下でなければ Inventory に入れる（装備ロジックと整合）
    if (Inventory && tool->Parent.lock().get() != Inventory.get()) {
        Inventory->addChild(std::static_pointer_cast<Instance>(tool));
    }

    Slots[slotIndex] = tool;
    return slotIndex;
}

void User::removeToolReferences(const std::shared_ptr<Tool>& tool) {
    if (!tool) return;
    for (auto& slot : Slots) {
        if (slot == tool) slot = nullptr;
    }
    if (currentTool == tool) {
        if (m_toolWeld) {
            if (auto parent = m_toolWeld->Parent.lock()) parent->removeChild(m_toolWeld->Name);
            m_toolWeld.reset();
        }
        currentTool = nullptr;
        currentSlotIndex = -1;
    }
}

int User::findSlotByName(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(Slots.size()); ++i) {
        if (Slots[i] && Slots[i]->Name == name) return i;
    }
    return -1;
}

std::shared_ptr<Tool> User::getToolInSlot(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(Slots.size())) return nullptr;
    return Slots[slotIndex];
}

std::shared_ptr<Tool> User::removeToolFromSlot(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(Slots.size())) return nullptr;
    auto tool = Slots[slotIndex];
    if (!tool) return nullptr;

    // 装備中なら解除する（character から外れる前に状態を整える）
    if (currentTool == tool) {
        if (m_toolWeld) {
            if (auto parent = m_toolWeld->Parent.lock()) parent->removeChild(m_toolWeld->Name);
            m_toolWeld.reset();
        }
        currentTool->Equipped = false;
        currentTool = nullptr;
        currentSlotIndex = -1;
    }

    // ツリー（Inventory もしくは character）からデタッチする
    if (auto parent = tool->Parent.lock()) {
        parent->removeChild(tool->Name);
    }

    Slots[slotIndex] = nullptr;
    return tool;
}

User::~User() {
    resetInputRuntimeState();
    if (s_instance == this) s_instance = nullptr;
    // shared_ptr なので参照カウントが 0 になれば自動解放される
    character = nullptr;
    humanoid  = nullptr;
}

void User::updateVectors() {
    // 外積を使わず、クォータニオンから直接ローカル軸を取り出す
    forward = cam.Orientation.getForward();
    right   = cam.Orientation.getRight();
    up      = cam.Orientation.getUp();
}

// ControlMode::Program 用: Luauからカメラを直接設定する
void User::setCameraCFrame(const CFrame& cf) {
    cpos            = cf.Position;
    cam.Orientation = cf.Rotation;
    updateVectors();
}

CFrame User::getCameraCFrame() const {
    return CFrame(cpos, cam.Orientation);
}

void User::updateMouseCapture() {
    if (!m_input) return;
    const bool wantsCapture = isRightMouseRotating || m_externalDragActive || m_mouseLockEnabled;
    if (wantsCapture == m_mouseCaptureApplied) return;
    m_input->setMouseCaptured(wantsCapture);
    m_mouseCaptureApplied = wantsCapture;
}

std::string User::toggleControlMode() {
    setControlMode(getControlMode() == ControlMode::Free ? ControlMode::Character : ControlMode::Free);
    return getControlMode() == ControlMode::Free ? "Free" : "Character";
}

bool User::toggleCtrlLock() { return setCtrlLockEnabled(!ctrlLockEnabled); }
bool User::setCtrlLockEnabled(bool enabled) { ctrlLockEnabled = enabled; return ctrlLockEnabled; }

std::string User::toggleCtrlLockOffset() {
    ctrlLockOffsetRight = !ctrlLockOffsetRight;
    return ctrlLockOffsetRight ? "Right" : "Left";
}

std::string User::setCtrlLockOffset(const std::string& side) {
    if (side == "Left") ctrlLockOffsetRight = false;
    else if (side == "Right") ctrlLockOffsetRight = true;
    return ctrlLockOffsetRight ? "Right" : "Left";
}

bool User::setMouseLockEnabled(bool enabled) {
    if (enabled && (!m_lastViewportFocused || !m_gameViewportCursorCenterValid ||
                    m_gameVpW <= 0.0f || m_gameVpH <= 0.0f))
        return false;
    m_mouseLockEnabled = enabled;
    if (m_mouseLockEnabled && m_input) {
        lastMouseX = m_gameViewportCursorCenterX;
        lastMouseY = m_gameViewportCursorCenterY;
        m_input->setCursorPos(lastMouseX, lastMouseY);
    }
    updateMouseCapture();
    return m_mouseLockEnabled;
}

bool User::toggleMouseLock() { return setMouseLockEnabled(!m_mouseLockEnabled); }

void User::setMoveDirection(const Vector3& direction) {
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        direction.lengthSquared() <= 0.0f) {
        clearMoveDirection();
        return;
    }
    m_scriptMoveDirection = direction.normalize();
    m_hasScriptMoveDirection = true;
}

void User::clearMoveDirection() {
    m_scriptMoveDirection = {};
    m_hasScriptMoveDirection = false;
}

void User::queueJump() { m_jumpQueued = true; }
void User::requestWorkspaceSwitch() { wantsSwitchWorkspace = true; }

Workspace* User::getCharacterWorkspace() const {
    if (!character) return nullptr;
    return dynamic_cast<Workspace*>(character->findFirstAncestorWorkspace());
}

bool User::moveCharacterToWorkspace(Workspace& destination) {
    if (!character) return false;
    auto current = getCharacterWorkspace();
    if (current == &destination) return true;

    const CFrame worldCFrame = character->getWorldCFrame();

    auto characterInstance = std::static_pointer_cast<Instance>(character);
    destination.addChild(characterInstance);
    if (getCharacterWorkspace() != &destination) return false;
    character->setWorldCFrame(worldCFrame);
    return true;
}

void User::requestExit() {
    if (ExitRequested && ExitRequested->hasListeners()) {
        if (!m_exitRequestPending) {
            m_exitRequestPending = true;
            ExitRequested->fire();
        }
    } else {
        m_exitRequestPending = false;
        wannaExit = true;
    }
}

void User::confirmExit() { m_exitRequestPending = false; wannaExit = true; }
void User::cancelExit() { m_exitRequestPending = false; }

bool User::selectToolSlot(int slot) {
    const int index = slot - 1;
    if (!character || index < 0 || index >= static_cast<int>(Slots.size())) return false;
    const int previous = currentSlotIndex;
    if (currentTool) {
        if (m_toolWeld) {
            if (auto parent = m_toolWeld->Parent.lock()) parent->removeChild(m_toolWeld->Name);
            m_toolWeld.reset();
        }
        currentTool->Equipped = false;
        character->removeChild(currentTool->Name);
        Inventory->addChild(std::static_pointer_cast<Instance>(currentTool));
        currentTool = nullptr;
        currentSlotIndex = -1;
    }
    if (previous == index || !Slots[index]) return previous == index;
    currentTool = Slots[index];
    currentTool->Equipped = true;
    Inventory->removeChild(currentTool->Name);
    character->addChild(std::static_pointer_cast<Instance>(currentTool));
    if (humanoid && currentTool->Handle) {
        const bool useLeft = currentTool->Hand == Tool::ToolHand::Left;
        auto arm = useLeft ? humanoid->getLeftArmPart() : humanoid->getRightArmPart();
        if (arm) {
            const CFrame target = arm->getWorldCFrame()
                * CFrame(Vector3(0.0f, 0.0f, -1.0f))
                * CFrame(currentTool->Position, currentTool->Rotation);
            currentTool->Handle->setWorldCFrame(target);
            m_toolWeld = std::make_shared<Weld>(arm, currentTool->Handle);
            m_toolWeld->Name = "ToolGrip";
            character->addChild(m_toolWeld);
        }
    }
    currentSlotIndex = index;
    return true;
}

bool User::activateTool() {
    if (!currentTool || !currentTool->Equipped) return false;
    currentTool->Activated->fire();
    return true;
}

void User::applyQueuedJump(Physics* physics) {
    if (!m_jumpQueued) return;
    m_jumpQueued = false;
    if (!humanoid) return;
    if (humanoid->isSeated()) {
        humanoid->standUp(physics);
        lastMovementInput.standUpRequested = true;
    } else {
        humanoid->jump(physics);
        lastMovementInput.jumpRequested = true;
    }
}

void User::resetInputRuntimeState() {
    m_mouseLockEnabled = false;
    isRightMouseRotating = false;
    m_externalDragActive = false;
    updateMouseCapture();
    m_altLookActive = false;
    m_altKeyWasDown = false;
    m_lastEscapeKeyPressed = false;
    m_lastF8KeyPressed = false;
    m_lastViewportFocused = false;
    m_jumpQueued = false;
    clearMoveDirection();
    m_exitRequestPending = false;
    wannaExit = false;
    wantsSwitchWorkspace = false;
    toolActivated = false;
    lastFKeyPressed = false;
    lastCtrlKeyPressed = false;
    lastCtrlLockFKeyPressed = false;
    lastToolKeyPressed.fill(false);
    m_gameViewportCursorCenterValid = false;
}

// カメラ回転（マウス右ドラッグ＋矢印キー）
bool User::processCameraRotation(bool viewportFocused, float deltaTime, bool builtinInputEnabled) {
    const bool physicalAltDown = m_input->isKeyDown(KeyCode::LeftAlt) ||
        m_input->isKeyDown(KeyCode::RightAlt);
    if (getControlMode() == ControlMode::Program) {
        // Program はカメラを回さない。右ドラッグ/Alt が残した一時捕捉だけを
        // 解放し、MouseLock と外部ドラッグの所有分は updateMouseCapture() に任せる。
        m_altLookActive = false;
        m_altKeyWasDown = physicalAltDown;
        if (isRightMouseRotating) {
            isRightMouseRotating = false;
            updateMouseCapture();
        }
        return false;
    }

    if (!builtinInputEnabled || !viewportFocused) {
        // カテゴリを無効化した瞬間に Alt/右ドラッグによる捕捉を残さない。
        // MouseLock は明示 API の持続状態なので、その回転入力は継続する。
        m_altLookActive = false;
        m_altKeyWasDown = physicalAltDown;
        if (isRightMouseRotating && !m_mouseLockEnabled) {
            isRightMouseRotating = false;
            updateMouseCapture();
        }
    }

    bool rotated = false;
    const float frameScale = std::max(deltaTime, 0.0f) * 60.0f;

    // Alt トグル: ビューポートにフォーカスがあるとき、Alt 押下の立ち上がりで
    // フリールック(マウスを動かすだけでカメラが回る)を ON/OFF する
    const bool altDown = physicalAltDown;
    if (altDown && !m_altKeyWasDown) m_altLookActive = !m_altLookActive;
    m_altKeyWasDown = altDown;
    // フォーカスを失ったらフリールックは解除（カーソルが隠れたままになるのを防ぐ）
    if (!viewportFocused) m_altLookActive = false;

    const bool rightMousePressed = builtinInputEnabled && viewportFocused && m_input->isMouseButtonDown(MouseButton::Right);
    const bool looking = rightMousePressed || m_altLookActive || m_mouseLockEnabled;

    if (looking) {
        if (!isRightMouseRotating) {
            // 開始: カーソルをロック(非表示)し、アンカー位置を取得する
            isRightMouseRotating = true;
            updateMouseCapture();
            if (m_mouseLockEnabled && m_gameViewportCursorCenterValid) {
                lastMouseX = m_gameViewportCursorCenterX;
                lastMouseY = m_gameViewportCursorCenterY;
                m_input->setCursorPos(lastMouseX, lastMouseY);
            } else {
                m_input->getCursorPos(lastMouseX, lastMouseY);
            }
        } else {
            // ロック中(DISABLED+raw)なので画面端クランプ・加速が無く滑らかな差分が得られる。
            // アンカーからの差分で回転したのち、仮想カーソルをアンカーへ戻す。
            // これにより ImGui へは固定位置を見せ、他エディター要素の誤反応を防ぐ。
            double currentMouseX = 0.0, currentMouseY = 0.0;
            m_input->getCursorPos(currentMouseX, currentMouseY);
            const double deltaX = currentMouseX - lastMouseX;
            const double deltaY = currentMouseY - lastMouseY;
            if (deltaX != 0.0 || deltaY != 0.0) {
                cam.Orientation =
                    Quaternion::fromAxisAngle(Vector3(0, 1, 0), static_cast<float>(-deltaX * mouseRotationSpeed)) *
                    cam.Orientation;
                cam.Orientation =
                    cam.Orientation *
                    Quaternion::fromAxisAngle(Vector3(1, 0, 0), static_cast<float>(-deltaY * mouseRotationSpeed));
                rotated = true;
            }
            m_input->setCursorPos(lastMouseX, lastMouseY);
        }
    } else {
        // ドラッグ終了(ボタン解除 or フォーカス喪失): カーソルロックを解放する
        if (isRightMouseRotating) {
            isRightMouseRotating = false;
            updateMouseCapture();
        }
    }

    if (builtinInputEnabled && viewportFocused) {
        const float keyboardRotation = rotationSpeed * frameScale;
        if (m_input->isKeyDown(KeyCode::Left))  { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0),  keyboardRotation) * cam.Orientation; rotated = true; }
        if (m_input->isKeyDown(KeyCode::Right)) { cam.Orientation = Quaternion::fromAxisAngle(Vector3(0,1,0), -keyboardRotation) * cam.Orientation; rotated = true; }
        if (m_input->isKeyDown(KeyCode::Up))    { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0),  keyboardRotation); rotated = true; }
        if (m_input->isKeyDown(KeyCode::Down))  { cam.Orientation = cam.Orientation * Quaternion::fromAxisAngle(Vector3(1,0,0), -keyboardRotation); rotated = true; }
    }

    if (rotated) updateVectors();
    return rotated;
}

void User::beginExternalCameraDrag() {
    if (m_externalDragActive) return;
    m_externalDragActive = true;
    updateMouseCapture();
    m_input->getCursorPos(lastMouseX, lastMouseY);
}
void User::sampleExternalCameraDrag(double& dx, double& dy) {
    dx = 0.0; dy = 0.0;
    if (!m_externalDragActive) return;
    double curX = 0.0, curY = 0.0;
    m_input->getCursorPos(curX, curY);
    dx = curX - lastMouseX;
    dy = curY - lastMouseY;
    m_input->setCursorPos(lastMouseX, lastMouseY);
}
void User::endExternalCameraDrag() {
    if (!m_externalDragActive) return;
    m_externalDragActive = false;
    updateMouseCapture();
}

// ズーム（I/Oキー・スクロール）
void User::processZoom(bool keyboardZoomEnabled, bool mouseZoomEnabled, float deltaTime) {
    // 無効時も毎フレーム破棄しておかないと、ビューポート外でのスクロールが
    // 消費されずに溜まり、後でhoverしただけの瞬間にまとめて適用されてしまう
    const double scrollDelta = m_input->consumeScrollDelta();
    if (getControlMode() == ControlMode::Program) return; // Luauがカメラを直接制御するため入力は無視する

    const float keyboardZoom = zoomSpeed * std::max(deltaTime, 0.0f) * 60.0f;
    if (getControlMode() == ControlMode::Free) {
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::I)) cpos = cpos + forward * keyboardZoom;
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::O)) cpos = cpos - forward * keyboardZoom;
        if (mouseZoomEnabled && scrollDelta != 0.0) {
            cpos = cpos + forward * (static_cast<float>(scrollDelta) * mouseZoomSpeed);
        }
    } else {
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::I)) { cameraDistance -= keyboardZoom; if (cameraDistance < minCameraDistance) cameraDistance = minCameraDistance; }
        if (keyboardZoomEnabled && m_input->isKeyDown(KeyCode::O)) cameraDistance += keyboardZoom;
        if (mouseZoomEnabled && scrollDelta != 0.0) {
            cameraDistance -= static_cast<float>(scrollDelta) * mouseZoomSpeed;
            if (cameraDistance < minCameraDistance) cameraDistance = minCameraDistance;
        }
    }
}

// 移動ディスパッチ（Free / Character を振り分け）
void User::processMovement(bool viewportFocused, Physics* physics, float deltaTime) {
    if (viewportFocused || m_hasScriptMoveDirection) {
        if (getControlMode() == ControlMode::Free) {
            Vector3 moveDirection{};

            if (m_hasScriptMoveDirection) {
                moveDirection = m_scriptMoveDirection;
            } else if (m_movementInputEnabled) {
                if (m_input->isKeyDown(KeyCode::W)) moveDirection += forward;
                if (m_input->isKeyDown(KeyCode::S)) moveDirection -= forward;
                if (m_input->isKeyDown(KeyCode::A)) moveDirection -= right;
                if (m_input->isKeyDown(KeyCode::D)) moveDirection += right;
                if (m_input->isKeyDown(KeyCode::Q)) moveDirection -= up;
                if (m_input->isKeyDown(KeyCode::E)) moveDirection += up;
            }

            const bool isMoving = moveDirection.lengthSquared() > 0.0f;

            if (isMoving) {
                movingTime += deltaTime;

                if (movingTime > accelerationDelay) {
                    accelerationMultiplier += accelerationRate * deltaTime;
                    if (maxAccelerationMultiplier < accelerationMultiplier) {
                        accelerationMultiplier = maxAccelerationMultiplier;
                    }
                }

                moveDirection = moveDirection.normalize();

                const float movementSpeed =
                    speed * std::max(deltaTime, 0.0f) * 60.0f * accelerationMultiplier;

                cpos += moveDirection * movementSpeed;
            }
            else {
                movingTime = 0.0f;
                accelerationMultiplier = 1.0f;
            }

        } else if (getControlMode() == ControlMode::Character && character && humanoid) {
            processCharacterMovement(physics, deltaTime);
        }
    }
}

void User::getToolArmRaiseState(bool& leftArmRaised, bool& rightArmRaised) const {
    bool toolEquipped = currentTool && currentTool->Equipped;
    leftArmRaised  = toolEquipped && currentTool->Hand == Tool::ToolHand::Left;
    rightArmRaised = toolEquipped && currentTool->Hand != Tool::ToolHand::Left;
}

// キャラクターの移動・カメラ追従（移動・回転・歩行アニメ・接地判定そのものはHumanoidが行う）
void User::processCharacterMovement(Physics* physics, float deltaTime) {
    if (!humanoid) return;
    auto root = humanoid->getRootPart();
    if (!root) return;

    // --- 入力方向の収集 ---
    Vector3 targetMoveDir(0, 0, 0);
    bool isPressingMove = false;

    Vector3 flatForward = Vector3(forward.x, 0, forward.z).normalize();
    Vector3 flatRight   = Vector3(right.x,   0, right.z  ).normalize();

    bool wDown = m_movementInputEnabled && m_input->isKeyDown(KeyCode::W);
    bool sDown = m_movementInputEnabled && m_input->isKeyDown(KeyCode::S);
    bool aDown = m_movementInputEnabled && m_input->isKeyDown(KeyCode::A);
    bool dDown = m_movementInputEnabled && m_input->isKeyDown(KeyCode::D);

    if (m_hasScriptMoveDirection) {
        targetMoveDir = m_scriptMoveDirection;
        isPressingMove = true;
        wDown = sDown = aDown = dDown = false;
    } else {
        if (wDown) { targetMoveDir = targetMoveDir + flatForward; isPressingMove = true; }
        if (sDown) { targetMoveDir = targetMoveDir - flatForward; isPressingMove = true; }
        if (aDown) { targetMoveDir = targetMoveDir - flatRight;   isPressingMove = true; }
        if (dDown) { targetMoveDir = targetMoveDir + flatRight;   isPressingMove = true; }
    }

    if (isPressingMove) targetMoveDir = targetMoveDir.normalize();

    // Truss登坂・Seat操作用の独立した2軸(-1..1)。斜め入力でも減衰しない生の値
    float forwardAxis = (wDown ? 1.0f : 0.0f) - (sDown ? 1.0f : 0.0f);
    float rightAxis   = (dDown ? 1.0f : 0.0f) - (aDown ? 1.0f : 0.0f);
    if (m_hasScriptMoveDirection) {
        forwardAxis = std::clamp(Vector3::Dot(m_scriptMoveDirection, flatForward), -1.0f, 1.0f);
        rightAxis = std::clamp(Vector3::Dot(m_scriptMoveDirection, flatRight), -1.0f, 1.0f);
    }

    bool leftArmRaised = false, rightArmRaised = false;
    getToolArmRaiseState(leftArmRaised, rightArmRaised);

    lastMovementInput.flatForward     = flatForward;
    lastMovementInput.flatRight       = flatRight;
    lastMovementInput.targetMoveDir   = targetMoveDir;
    lastMovementInput.isPressingMove  = isPressingMove;
    lastMovementInput.ctrlLockEnabled = ctrlLockEnabled;
    lastMovementInput.forwardAxis     = forwardAxis;
    lastMovementInput.rightAxis       = rightAxis;

    humanoid->move(flatForward, flatRight, isPressingMove, targetMoveDir, ctrlLockEnabled, physics,
                   leftArmRaised, rightArmRaised, forwardAxis, rightAxis, characterSmoothing, deltaTime);

    // --- カメラ追従 ---
    const Vector3 headOffset = Vector3(0, 2.5f, 0); // Humanoid::applyBodyAnimation()のheadOffsetと一致させる
    if (humanoid->isInFirstPerson()) {
        cpos = humanoid->getRootWorldPosition() + headOffset;
    } else {
        Vector3 basePos = humanoid->getRootWorldPosition() + Vector3(0, 2.0f, 0) - (forward * cameraDistance);
        if (ctrlLockEnabled) {
            float offsetSign = ctrlLockOffsetRight ? 1.0f : -1.0f;
            basePos = basePos + right * (ctrlLockOffsetDistance * offsetSign);
        }
        cpos = basePos;
    }
}

// ホットキー（ESC / L / Space）
void User::processHotkeys(Physics* physics) {
    const bool escapePressed = m_input->isKeyDown(KeyCode::Escape);
    if (escapePressed && !m_lastEscapeKeyPressed) requestExit();
    m_lastEscapeKeyPressed = escapePressed;

    // Lキー: Free/Character モード切り替え
    bool fPressed = m_input->isKeyDown(KeyCode::L);
    if (fPressed && !lastFKeyPressed && allowControlModeSwitch) {
        RCBN_LOG("Control Mode: " << toggleControlMode());
    }
    lastFKeyPressed = fPressed;

    // 左Ctrlキー: CtrlLock ON/OFFトグル
    bool ctrlKeyPressed = m_input->isKeyDown(KeyCode::LeftControl);
    if (ctrlKeyPressed && !lastCtrlKeyPressed && getControlMode() == ControlMode::Character) {
        toggleCtrlLock();
        RCBN_LOG(ctrlLockEnabled ? "CtrlLock: ON" : "CtrlLock: OFF");
    }
    lastCtrlKeyPressed = ctrlKeyPressed;

    // Fキー: CtrlLockのオフセット方向（左右）切り替え（CtrlLockのON/OFFに関わらず状態は保持される）
    bool ctrlLockFKeyPressed = m_input->isKeyDown(KeyCode::F);
    if (ctrlLockFKeyPressed && !lastCtrlLockFKeyPressed) {
        toggleCtrlLockOffset();
        // RCBN_LOG(ctrlLockOffsetRight ? "CtrlLock offset: Right" : "CtrlLock offset: Left");
    }
    lastCtrlLockFKeyPressed = ctrlLockFKeyPressed;
}

bool User::consumeExitRequest() {
    bool v = wannaExit;
    wannaExit = false;
    return v;
}

bool User::consumeWorkspaceSwitchRequest() {
    bool v = wantsSwitchWorkspace;
    wantsSwitchWorkspace = false;
    return v;
}

void User::processToolkeys(bool viewportFocused, bool isGameplayInput, bool wantsTextInput) {
    static const KeyCode keys[] = {
        KeyCode::Num1, KeyCode::Num2, KeyCode::Num3, KeyCode::Num4, KeyCode::Num5,
        KeyCode::Num6, KeyCode::Num7, KeyCode::Num8, KeyCode::Num9, KeyCode::Num0
    };
    const bool actionsEnabled = isGameplayInput && character && viewportFocused &&
        getControlMode() == ControlMode::Character && !wantsTextInput;

    for (int i = 0; i < 10; i++) {
        const bool pressed = m_input->isKeyDown(keys[i]);

        if (actionsEnabled && pressed && !lastToolKeyPressed[i]) {
            RCBN_TRACE("Tool key pressed: " + std::to_string(i + 1));
            selectToolSlot(i + 1);
        }

        lastToolKeyPressed[i] = pressed;
    }
}

void User::processMouse(bool isGameplayInput) {
    if (m_input->isMouseButtonDown(MouseButton::Left)) {
        if (!toolActivated && currentTool && currentTool->Equipped && isGameplayInput) {
            activateTool();
        }
        // 抑止中の押下も同期し、有効化直後の遅延発火を防ぐ。
        toolActivated = true;
    } else {
        toolActivated = m_input->isMouseButtonDown(MouseButton::Left);
    }
}

// ============================================================
// processInput（呼び出し口）
// ============================================================

void User::processInput(Physics* physics, float deltaTime, bool viewportFocused,
                        bool viewportHovered, bool isGameplayInput, bool wantsTextInput) {
    if (!m_input) return;
    m_lastViewportFocused = viewportFocused;

    // 外部setterで前フレーム間にFree/Programへ切り替えられた場合も、
    // Character入力が残した水平速度を最初の物理更新前に止める。
    if (m_lastProcessedControlMode &&
        *m_lastProcessedControlMode == ControlMode::Character &&
        getControlMode() != ControlMode::Character && humanoid) {
        humanoid->stopCharacterMotion(physics);
    }
    m_lastProcessedControlMode = getControlMode();

    // ジャンプ要求は毎フレームクリアし、processHotkeys()内でSpace押下時にのみセットする
    // (ネットワークレプリケーション用: このフレームでジャンプ要求があったかをlastMovementInputに残す)
    lastMovementInput.jumpRequested = false;
    lastMovementInput.standUpRequested = false;

    // User.Input: 前フレームとの差分で Pressed/Released を発火する
    if (Input) Input->poll();

    // MouseLockはフォーカスを失った瞬間に解除する。Escapeでは解除しない。
    if (!viewportFocused && m_mouseLockEnabled) setMouseLockEnabled(false);

    const bool f8Pressed = m_input->isKeyDown(KeyCode::F8);
    if (f8Pressed && !m_lastF8KeyPressed && isGameplayInput && viewportFocused &&
        !wantsTextInput && m_cameraInputEnabled) {
        toggleMouseLock();
    }
    m_lastF8KeyPressed = f8Pressed;

    // 死亡後の経過時間はHumanoid側で管理し、再生成だけUserが行う
    if (humanoid && humanoid->isRespawnReady()) respawnCharacter();

    bool rotated = processCameraRotation(viewportFocused && !wantsTextInput, deltaTime, m_cameraInputEnabled);
    if (m_cameraInputEnabled) {
        processZoom(viewportFocused && !wantsTextInput,
                    viewportHovered && !wantsTextInput,
                    deltaTime);
    } else {
        // 無効中もスクロールは溜めない。
        m_input->consumeScrollDelta();
    }
    if (humanoid) humanoid->updateFirstPersonState(cameraDistance <= firstPersonThreshold);
    // 死亡中はキャラクター移動を駆動しない（ばらしたパーツを上書きしないため）
    if (!humanoid || !humanoid->isDead() || getControlMode() == ControlMode::Free) {
        processMovement(viewportFocused && !wantsTextInput, physics, deltaTime);
    }
    if (humanoid && character) humanoid->updatePhysicsState(physics);
    if (rotated) updateVectors();
    if (m_movementInputEnabled && !wantsTextInput && getControlMode() == ControlMode::Character &&
        m_input->isKeyDown(KeyCode::Space)) {
        queueJump();
    }
    if (m_hotkeyInputEnabled && !wantsTextInput) {
        processHotkeys(physics);
    } else {
        m_lastEscapeKeyPressed = m_input->isKeyDown(KeyCode::Escape);
        lastFKeyPressed = m_input->isKeyDown(KeyCode::L);
        lastCtrlKeyPressed = m_input->isKeyDown(KeyCode::LeftControl);
        lastCtrlLockFKeyPressed = m_input->isKeyDown(KeyCode::F);
    }

    // Lキー切替はこのフレームの入力処理後に発生するため、同じフレーム内で
    // Characterの水平速度を止める。Y速度とhoverのjump抑制状態は変更しない。
    if (m_lastProcessedControlMode == ControlMode::Character &&
        getControlMode() != ControlMode::Character && humanoid) {
        humanoid->stopCharacterMotion(physics);
    }
    m_lastProcessedControlMode = getControlMode();
    if (m_toolInputEnabled) {
        processToolkeys(viewportFocused, isGameplayInput, wantsTextInput);
        processMouse(isGameplayInput && !wantsTextInput);
    } else {
        toolActivated = m_input->isMouseButtonDown(MouseButton::Left);
        static const KeyCode keys[] = { KeyCode::Num1, KeyCode::Num2, KeyCode::Num3, KeyCode::Num4, KeyCode::Num5,
            KeyCode::Num6, KeyCode::Num7, KeyCode::Num8, KeyCode::Num9, KeyCode::Num0 };
        for (int i = 0; i < 10; ++i) lastToolKeyPressed[i] = m_input->isKeyDown(keys[i]);
    }
    applyQueuedJump(physics);
}


void User::despawnCharacter() {
    if (!character) return;
    auto parent = character->Parent.lock();
    if (parent) {
        parent->removeChild(character->Name);
    }
    character = nullptr;
    humanoid  = nullptr;
}

void User::respawnCharacter() {
    // 死亡したキャラクターが属していた親(Workspace)を保持してから作り直す
    std::shared_ptr<Instance> parent = character ? character->Parent.lock() : nullptr;
    auto equippedTool = currentTool;
    const int equippedSlotIndex = currentSlotIndex;
    const bool shouldRestoreTool =
        equippedTool && equippedTool->Equipped && character && Inventory &&
        equippedSlotIndex >= 0 &&
        equippedSlotIndex < static_cast<int>(Slots.size()) &&
        Slots[equippedSlotIndex] == equippedTool &&
        equippedTool->Parent.lock().get() == character.get();

    // 装備中Toolを旧characterに残したままdespawnすると、Slots/currentToolの
    // 強参照によって死亡地点の物理状態ごと生存する。先にWorkspace外へ退避し、
    // actorを解放してから新characterを生成する。
    if (shouldRestoreTool) {
        equippedTool->Equipped = false;
        character->removeChild(equippedTool->Name);
        Inventory->addChild(std::static_pointer_cast<Instance>(equippedTool));
    }

    despawnCharacter();
    spawnCharacter(m_lastSearchRoot, m_lastSpawnWorkspace);
    if (parent && character) {
        parent->addChild(character);
    }

    if (!shouldRestoreTool) return;

    if (character) {
        Inventory->removeChild(equippedTool->Name);
        character->addChild(std::static_pointer_cast<Instance>(equippedTool));
        if (equippedTool->Parent.lock().get() == character.get()) {
            equippedTool->Equipped = true;
            currentTool = equippedTool;
            currentSlotIndex = equippedSlotIndex;
            return;
        }

        // 名前衝突等で再装備に失敗した場合もToolを失わない。
        Inventory->addChild(std::static_pointer_cast<Instance>(equippedTool));
    }

    equippedTool->Equipped = false;
    currentTool = nullptr;
    currentSlotIndex = -1;
}

void User::setCharacterFromScript(std::shared_ptr<Model> newCharacter) {
    if (!newCharacter) {
        character = nullptr;
        humanoid = nullptr;
        setControlMode(ControlMode::Free);
        return;
    }

    character = newCharacter;

    auto it = character->getChildren().find("Humanoid");
    humanoid = (it != character->getChildren().end()) ? std::dynamic_pointer_cast<Humanoid>(it->second) : nullptr;
    if (humanoid) humanoid->resolveParts(character.get());

    if (getControlMode() == ControlMode::Free) setControlMode(ControlMode::Character);

    if (CharacterAdded) {
        auto self = character;
        CharacterAdded->fire([self](lua_State* L) -> int {
            LuauEngine::pushInstance(L, std::static_pointer_cast<Instance>(self));
            return 1;
        });
    }
}

// System配下を再帰探索してStarterCharacterを見つける
static Instance* findStarterCharacter(Instance* inst) {
    if (!inst) return nullptr;
    if (inst->getClassName() == "StarterCharacter") return inst;
    for (auto& [name, child] : inst->children) {
        if (auto* found = findStarterCharacter(child.get())) return found;
    }
    return nullptr;
}

// StarterCharacterが一つも無いプロジェクトのためのフォールバック。
// 以前ハードコードされていた既定のリグ(Humanoid+Root+Torso+Head+両腕+両脚)を
// そのままStarterCharacterとして生成する(初回のみ。以後はsearchRootの子として見つかる)。
static std::shared_ptr<StarterCharacter> createDefaultStarterCharacter() {
    auto starter = std::make_shared<StarterCharacter>();
    starter->Name = "StarterCharacter";
    CharacterRig::buildDefaultRigParts(starter);
    return starter;
}

std::shared_ptr<Model> User::buildCharacterModel(
    Instance* searchRoot,
    const std::string& name
) {
    Instance* starter = findStarterCharacter(searchRoot);

    if (!starter && searchRoot) {
        auto defaultStarter = createDefaultStarterCharacter();
        searchRoot->addChild(defaultStarter);
        starter = defaultStarter.get();

        RCBN_LOG(
            "StarterCharacter が見つからなかったため、"
            "既定のキャラクターを生成しました"
        );
    }

    if (!starter) {
        return nullptr;
    }

    auto model = std::make_shared<Model>(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 1.0f, 1.0f)
    );

    model->Name = name;

    // StarterCharacter直下の全Instanceを一つのForestとしてcloneする。
    // こうすることで、別サブツリーを指すWeld等も
    // old Instance -> cloned Instance の共通mapで再配線される。
    std::vector<std::shared_ptr<Instance>> roots;
    roots.reserve(starter->children.size());

    for (const auto& [childName, child] : starter->children) {
        if (child) {
            roots.push_back(child);
        }
    }

    auto clonedChildren = Instance::cloneForest(roots);

    for (auto& clonedChild : clonedChildren) {
        if (clonedChild) {
            model->addChild(clonedChild);
        }
    }

    // rebindClonedConstraints() は不要。
    // cloneForest() が全root共通のCloneRemapを作り、
    // remapClonedInstances()まで実行する。

    auto humanoidIt = model->getChildren().find("Humanoid");

    if (humanoidIt != model->getChildren().end()) {
        if (
            auto characterHumanoid =
                std::dynamic_pointer_cast<Humanoid>(humanoidIt->second);

            characterHumanoid &&
            !characterHumanoid->getWalkAnimation() &&
            characterHumanoid->getWalkAnimationPath().empty()
        ) {
            auto fallback = std::make_shared<Animation>();
            fallback->Name = "R6Walk";
            fallback->setBuiltInClip(
                AnimationClip::defaultR6Walk()
            );

            model->addChild(fallback);
            characterHumanoid->setWalkAnimation(fallback);
        }
    }

    return model;
}

static void moveSpatialSubtreeByWorldDelta(
    const std::shared_ptr<Instance>& root,
    const CFrame& delta
) {
    if (!root) return;

    struct SpatialMove {
        std::shared_ptr<Spatial> spatial;
        CFrame worldCFrame;
    };

    std::vector<SpatialMove> moves;

    std::function<void(const std::shared_ptr<Instance>&)> collect =
        [&](const std::shared_ptr<Instance>& instance) {
            if (!instance) return;

            if (auto spatial = std::dynamic_pointer_cast<Spatial>(instance)) {
                moves.push_back({
                    spatial,
                    spatial->getWorldCFrame()
                });
            }

            for (const auto& [name, child] : instance->children) {
                collect(child);
            }
        };

    collect(root);

    // 先に全WorldCFrameを保存してあるので、
    // setterによる親子座標の再計算に影響されない。
    for (auto& move : moves) {
        move.spatial->setWorldCFrame(
            delta * move.worldCFrame
        );
    }
}

void User::placeCharacterAtSpawn(
    const std::shared_ptr<Model>& model,
    const std::shared_ptr<Humanoid>& humanoid,
    Workspace* workspace,
    std::uint32_t spawnPeerId) {
    if (!model || !humanoid) return;
    auto root = humanoid->getRootPart();
    if (!root) return;

    std::vector<std::shared_ptr<SpawnLocation>> candidates;
    auto collect = [&](auto& self, const std::shared_ptr<Instance>& node) -> void {
        if (!node) return;
        if (auto spawn = std::dynamic_pointer_cast<SpawnLocation>(node);
            spawn && spawn->Enabled) {
            candidates.push_back(std::move(spawn));
        }
        for (const auto& [name, child] : node->children) {
            (void)name;
            self(self, child);
        }
    };
    if (workspace) {
        for (const auto& [name, child] : workspace->children) {
            (void)name;
            collect(collect, child);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& first, const auto& second) {
            return first->getFullPath() < second->getFullPath();
        });

    const CFrame currentRoot = root->getWorldCFrame();
    CFrame targetRoot = currentRoot;
    if (!candidates.empty()) {
        const std::size_t index = spawnPeerId == 0
            ? 0
            : static_cast<std::size_t>(spawnPeerId - 1) % candidates.size();
        const auto& spawn = candidates[index];
        // An explicit HipHeight is a user-authored Root-to-ground distance.
        targetRoot = spawn->getWorldCFrame();
        if (humanoid->isHipHeightExplicitlySet()) {
            targetRoot = targetRoot * CFrame(
                0.0f,
                spawn->Size.y * 0.5f + humanoid->getHipHeight(),
                0.0f
            );
        } else {
            // An unset HipHeight must not turn SpawnLocation selection into a
            // Root height correction. Keep the authored Root Y and let the
            // first valid ground sample capture its Root-to-ground distance.
            targetRoot.Position.y = currentRoot.Position.y;
        }
    }
    const CFrame delta = targetRoot * currentRoot.inverse();

    moveSpatialSubtreeByWorldDelta(
        std::static_pointer_cast<Instance>(model),
        delta
    );
}

void User::spawnCharacter(Instance* searchRoot, Workspace* workspace,
                          const std::optional<Vector3>& initialPosition) {
    m_lastSearchRoot = searchRoot; // respawn 用に保持
    m_lastSpawnWorkspace = workspace;
    if (character) {
        despawnCharacter();
    }

    // HACK: ただのテスト配列
    // auto tool1 = std::make_shared<Tool>("TestTool1");
    // auto tool2 = std::make_shared<Tool>("TestTool2");
    // std::shared_ptr<Cube> testHandle = std::make_shared<Cube>(Vector3(0,0,0), Vector3(0.5f, 0.5f, 5.0f), 0);
    // testHandle->Name = "Handle";
    // testHandle->Anchored = true;
    // testHandle->Color = Color4::FromRGB(255, 0, 0);
    // testHandle->CanCollide = false;

    // tool1->addChild(testHandle);
    // tool1->Hand = Tool::ToolHand::Left;
    // tool1->Handle = testHandle;

    // Slots[0] = tool1;
    // Slots[1] = tool2;
    // Inventory->addChild(tool1);
    // Inventory->addChild(tool2);
    // end of HACK

    const std::string characterName = peerId != 0 ? NetworkIdentity::characterName(peerId) : "PlayerCharacter";
    character = buildCharacterModel(searchRoot, characterName);
    if (!character) {
        RCBN_WARN("User::spawnCharacter: StarterCharacter を生成できないため、キャラクターは生成されません");
        return;
    }
    if (peerId != 0) character->lockRuntimeName();
    auto it = character->getChildren().find("Humanoid");
    humanoid = (it != character->getChildren().end()) ? std::dynamic_pointer_cast<Humanoid>(it->second) : nullptr;
    if (humanoid) {
        humanoid->resolveParts(character.get());
        if (auto root = humanoid->getRootPart()) {
            root->Anchored = false;
            root->CanCollide = true;
        }
    }
    
    if (initialPosition) {
        const CFrame current = character->getWorldCFrame();

        CFrame target = current;
        target.Position = *initialPosition;

        const CFrame delta = target * current.inverse();

        moveSpatialSubtreeByWorldDelta(
            std::static_pointer_cast<Instance>(character),
            delta
        );
    }
    else {
        placeCharacterAtSpawn(
            character,
            humanoid,
            workspace,
            peerId
        );
    }

    // NOTE: この時点ではcharacterはまだWorkspaceに追加されていない(addChildは呼び出し元が行う)。
    // それでもRoot等のパーツ解決は完了しているため、Luau側はcharacterを直接受け取れば
    // workspace:WaitChild()に頼らずrespawn後の新しいキャラクターを取得できる
    if (CharacterAdded) {
        auto self = character;
        CharacterAdded->fire([self](lua_State* L) -> int {
            LuauEngine::pushInstance(L, std::static_pointer_cast<Instance>(self));
            return 1;
        });
    }
    RCBN_LOG("Spawning character...");
}

std::string User::getClassName() {
    return "User";
}

bool User::IsA(std::string className) {
    if (className == "User") return true;
    return Instance::IsA(className);
}

void User::setProperty(const std::string& name, const YAML::Node& value) {
    if (PropertyRegistry::loadProperty(this, "User", name, value)) return;
    if (name == "CursorType") {
        const std::string s = value.as<std::string>();
        if (s == "Type1") setCursorType(CursorType::Type1); else if (s == "Type2") setCursorType(CursorType::Type2);
        else if (s == "Type3") setCursorType(CursorType::Type3); else if (s == "Type4") setCursorType(CursorType::Type4);
        else if (s == "Type5") setCursorType(CursorType::Type5); else if (s == "Type6") setCursorType(CursorType::Type6);
        else if (s == "Type7") setCursorType(CursorType::Type7); else if (s == "Type8") setCursorType(CursorType::Type8);
        else if (s == "Type9") setCursorType(CursorType::Type9); else if (s == "Type10") setCursorType(CursorType::Type10);
        else setCursorType(CursorType::Default);
        return;
    }
    if (name == "CursorImages" && value.IsSequence()) {
        for (const auto& item : value) {
            if (!item["Type"] || !item["Type"].IsScalar()) continue;
            const std::string type = item["Type"].as<std::string>();
            int index = -1;
            static constexpr const char* names[] = {"Type1", "Type2", "Type3", "Type4", "Type5",
                "Type6", "Type7", "Type8", "Type9", "Type10"};
            for (int candidate = 0; candidate < 10; ++candidate)
                if (type == names[candidate]) { index = candidate; break; }
            if (index < 0 || index >= 10) continue;
            if (item["ContentPath"]) setCursorImagePath(index, item["ContentPath"].as<std::string>());
            const auto hs = item["Hotspot"];
            if (hs && hs.IsSequence() && hs.size() >= 2) { setCursorHotspotX(index, hs[0].as<int>()); setCursorHotspotY(index, hs[1].as<int>()); }
            if (item["Size"]) setCursorSize(index, item["Size"].as<int>());
        }
        return;
    }
    Instance::setProperty(name, value);
}

std::shared_ptr<Instance> User::clone() const {
    // User is not cloneable due to its ownership of an IInputBackend (input source)
    // Return nullptr or throw an error
    RCBN_ERROR("User::clone() is not supported");
    return nullptr;
}
