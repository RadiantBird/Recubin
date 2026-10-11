#include <Network/Replication.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/ByteStream.hpp>
#include <Network/NetworkIdentity.hpp>

#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#include <Core/Physics.hpp>
#include <Core/CharacterRig.hpp>
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Seat.hpp>
#include <Instances/System.hpp>
#include <Instances/Gyro.hpp>
#include <Instances/Cube.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <cmath>
#include <unordered_set>

#ifdef max
#undef max
#endif

namespace {
bool isFiniteWireVector(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

HumanoidControlInput toControlInput(const AvatarInputWire& in) {
    HumanoidControlInput control;
    control.flatForward = in.flatForward;
    control.flatRight = in.flatRight;
    control.targetMoveDir = in.targetMoveDir;
    control.isPressingMove = in.isPressingMove;
    control.ctrlLockEnabled = in.ctrlLockEnabled;
    control.forwardAxis = in.forwardAxis;
    control.rightAxis = in.rightAxis;
    control.smoothing = in.smoothing;
    return control;
}

// 入力の移動部分だけをHumanoidへ渡す(単発アクションは呼び出し側で扱う)
void applyMovementInput(Humanoid& humanoid, const AvatarInputWire& in) {
    if (in.hasInput) humanoid.setControlInput(toControlInput(in));
    else humanoid.clearControlInput();
}
}

ReplicationManager::ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot)
    : m_workspace(workspace), m_user(user), m_characterSearchRoot(characterSearchRoot),
      m_physics(workspace ? workspace->getPhysicsEngine() : nullptr) {
}

ReplicationManager::~ReplicationManager() {
    // Client中に止めた接触着席を、ネットワーク終了後のローカルPlayへ持ち越さない
    Seat::setTouchSitEnabled(true);
    // Humanoidに登録したstepごとのコールバックは、このオブジェクトを参照している
    clientDetachInputRecording();
    for (auto& [id, avatar] : m_remoteAvatars) {
        (void)id;
        if (avatar.humanoid) avatar.humanoid->setControlStepCallback({});
    }
}

void ReplicationManager::setWorkspace(std::shared_ptr<Workspace> workspace) {
    Physics* newPhysics = workspace ? workspace->getPhysicsEngine() : nullptr;
    if (workspace == m_workspace) {
        m_physics = newPhysics;
        return;
    }

    std::uint32_t inheritedTick = 0;
    float inheritedAlpha = 0.0f;
    const bool inheritClock = m_physics != nullptr;
    if (m_physics)
        m_physics->getSynchronizedSimulationClock(
            inheritedTick, inheritedAlpha);

    // 旧Workspaceがまだ正本である間に、そこへ追加したproxyとAnchored変更を
    // 対称に解除する。
    despawnAllAvatars();
    clientReleaseWorldObjects();
    m_latestPoses.clear();
    m_latestVels.clear();
    m_inputQueues.clear();
    m_lastProcessedSeq.clear();
    m_sentSeatPaths.clear();
    m_seatStateRosterSize = 0;
    m_pathToNetId.clear();
    m_hostObjects.clear();
    m_clientObjects.clear();

    // WorkspaceとPhysicsは同じ更新点で交換し、以降のpacket/updateから旧worldを
    // 参照できないよう世代を進める。
    m_workspace = std::move(workspace);
    m_physics = newPhysics;
    ++m_workspaceGeneration;
    m_warnedPhysicsMismatch = false;

    m_avatarSendTimer = 0.0f;
    m_simulationClockTimer = 0.0f;
    m_simulationClockRosterSize = 0;
    m_worldRescanTimer = 1.0f;
    m_worldSendTimer = 0.0f;
    m_worldSnapshotTimer = 0.0f;
    m_worldSnapshotPending = true;
    m_worldMappingDirty = true;
    m_prevRosterSize = 0;
    m_nextNetId = 1;
    m_hasHostAuthoritativeSelfPose = false;
    m_inputHistory.clear();
    m_hasHostTick = false;
    m_hasRenderTick = false;

    const NetworkRole role = NetworkManager::get().getRole();
    m_forceReliableSimulationClock = role == NetworkRole::Host;
    if (m_physics) {
        if (role == NetworkRole::Offline) {
            m_physics->resetSimulationClockSynchronization();
        } else if (inheritClock) {
            m_physics->synchronizeSimulationClock(
                inheritedTick, inheritedAlpha);
        }
        if (role == NetworkRole::Host) {
            m_physics->makeSimulationClockAuthoritative();
            m_forceReliableSimulationClock = true;
        }
    }
}

void ReplicationManager::update(float dt, Physics* physics) {
    auto& net = NetworkManager::get();
    Physics* boundPhysics =
        m_workspace ? m_workspace->getPhysicsEngine() : nullptr;
    if (physics && physics != boundPhysics) {
        if (!m_warnedPhysicsMismatch) {
            RCBN_WARN("Replication update rejected a Physics pointer from a "
                      "different active Workspace");
            m_warnedPhysicsMismatch = true;
        }
        return;
    }
    m_warnedPhysicsMismatch = false;
    m_physics = boundPhysics;
    // 着席はHostが決める。Clientでは接触による自動着席をしない(SeatStateで反映する)
    Seat::setTouchSitEnabled(!(net.isActive() && net.getRole() == NetworkRole::Client));
    if (!net.isActive()) return;

    reconcileAvatars();

    if (net.getRole() == NetworkRole::Host) {
        if (m_pendingProxyUpgradeAll) {
            // hostSimulateAvatars()の通常経路に任せる。ここで即時upgradeすると、
            // WorkspaceのpendingConstraintsがflushされる前にproxy化・moveされうる。
            m_pendingProxyUpgradeAll = false;
        }
        hostSimulateAvatars(dt, m_physics);
    }

    sendAvatarUpdates(dt);
    if (net.getRole() == NetworkRole::Client) clientAdvanceRenderClock(dt);
    applyAvatarPoses(dt);

    if (net.getRole() == NetworkRole::Host) {
        hostSendSimulationClock(dt, m_physics);
        hostSendSeatStates();
        hostUpdateWorld(dt);
    } else if (net.getRole() == NetworkRole::Client) {
        clientEnsureInputRecording();
        clientSendInputs();
        reconcileLocalPose();
        clientApplyWorldSmoothing(dt);
    }
}

void ReplicationManager::hostSendSimulationClock(float dt, Physics* physics) {
    if (!physics) return;
    auto& net = NetworkManager::get();
    const size_t rosterSize = net.getRoster().size();
    if (rosterSize > m_simulationClockRosterSize)
        m_forceReliableSimulationClock = true;
    m_simulationClockRosterSize = rosterSize;
    m_simulationClockTimer += (std::max)(dt, 0.0f);
    const bool periodic = m_simulationClockTimer >= 0.2f;
    if (!periodic && !m_forceReliableSimulationClock) return;

    std::uint32_t tick = 0;
    float alpha = 0.0f;
    physics->getSynchronizedSimulationClock(tick, alpha);
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(MessageType::SimulationClock));
    writer.writeU32(tick);
    writer.writeF32(alpha);
    if (m_forceReliableSimulationClock &&
        net.sendBytes(writer.data, NetworkChannel::Reliable))
        m_forceReliableSimulationClock = false;
    if (periodic)
        net.sendBytes(writer.data, NetworkChannel::Unreliable);
    if (periodic) m_simulationClockTimer = 0.0f;
}

void ReplicationManager::hostSendSeatStates() {
    auto& net = NetworkManager::get();
    const size_t rosterSize = net.getRoster().size();
    const bool resendAll = rosterSize > m_seatStateRosterSize;
    m_seatStateRosterSize = rosterSize;

    auto seatPathOf = [](const std::shared_ptr<Humanoid>& humanoid) {
        const auto seat = humanoid ? humanoid->getSeat() : nullptr;
        return seat ? seat->getWorkspaceRelativePath() : std::string();
    };
    std::unordered_map<PeerId, std::string> current;
    if (net.isLocalPlayer() && m_user && m_user->humanoid)
        current[net.getLocalPeerId()] = seatPathOf(m_user->humanoid);
    for (const auto& [id, avatar] : m_remoteAvatars)
        current[id] = seatPathOf(avatar.humanoid);
    std::erase_if(m_sentSeatPaths, [&](const auto& entry) {
        return !current.contains(entry.first);
    });

    std::vector<std::pair<PeerId, std::string>> changed;
    for (const auto& [id, path] : current) {
        const auto sent = m_sentSeatPaths.find(id);
        if (resendAll || sent == m_sentSeatPaths.end() || sent->second != path)
            changed.emplace_back(id, path);
    }
    if (changed.empty()) return;

    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(MessageType::SeatState));
    writer.writeU16(static_cast<uint16_t>(changed.size()));
    for (const auto& [id, path] : changed) {
        const size_t length = (std::min)(path.size(), static_cast<size_t>(UINT16_MAX));
        writer.writeU32(id);
        writer.writeU16(static_cast<uint16_t>(length));
        for (size_t i = 0; i < length; ++i) writer.writeU8(static_cast<uint8_t>(path[i]));
    }
    // 送れなかった(まだ相手がいない)ときは記録せず、次のフレームで送り直す
    if (!net.sendBytes(writer.data, NetworkChannel::Reliable)) return;
    for (const auto& [id, path] : changed) m_sentSeatPaths[id] = path;
}

void ReplicationManager::clientApplySeatState(const uint8_t* payload, size_t len) {
    ByteReader reader{payload, len};
    uint16_t count = 0;
    if (!reader.readU16(count)) return;
    const PeerId localId = NetworkManager::get().getLocalPeerId();
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        uint16_t pathLength = 0;
        if (!reader.readU32(id) || !reader.readU16(pathLength)) return;
        if (reader.remaining < pathLength) return;
        std::string path(reinterpret_cast<const char*>(reader.p), pathLength);
        reader.p += pathLength;
        reader.remaining -= pathLength;
        // リモートアバターはAvatarBatchの着席フラグで見た目だけ合わせる(ローカルで溶接しない)
        if (id == localId) applyLocalSeatPath(path);
    }
}

void ReplicationManager::applyLocalSeatPath(const std::string& seatPath) {
    if (!m_user || !m_user->humanoid || !m_physics) return;
    const auto humanoid = m_user->humanoid;
    if (seatPath.empty()) {
        if (humanoid->isSeated()) humanoid->standUp(m_physics);
        return;
    }
    Instance* found = m_workspace ? m_workspace->getChildByPath(seatPath) : nullptr;
    const auto seat = found
        ? std::dynamic_pointer_cast<Seat>(found->shared_from_this()) : nullptr;
    if (!seat) {
        RCBN_WARN("Replication: SeatState refers to a missing Seat: " << seatPath);
        return;
    }
    if (humanoid->getSeat() == seat) return;
    seat->sit(humanoid);
}

bool ReplicationManager::getLocalRootCFrame(CFrame& out) const {
    if (!m_user || !m_user->character) return false;

    for (auto& [name, child] : m_user->character->getChildren()) {
        if (name != "Root") continue;
        auto root = std::dynamic_pointer_cast<BaseCube>(child);
        if (!root) return false;
        out = root->getWorldCFrame();
        return true;
    }
    return false;
}

void ReplicationManager::sendAvatarUpdates(float dt) {
    auto& net = NetworkManager::get();

    m_avatarSendTimer += dt;
    if (m_avatarSendTimer < 0.05f) return;
    m_avatarSendTimer = 0.0f;

    // Clientの入力はclientSendInputs()が毎フレーム送る
    if (net.getRole() == NetworkRole::Host) {
        CFrame local;
        if (net.isLocalPlayer() && getLocalRootCFrame(local)) {
            m_latestPoses[net.getLocalPeerId()] = local;

            // プレイヤーホストの速度を記録してからAvatarBatchに載せる。
            Vector3 localVel{};
            auto localRoot = m_user && m_user->humanoid
                ? m_user->humanoid->getRootPart() : nullptr;
            Physics* physics = m_workspace ? m_workspace->getPhysicsEngine() : nullptr;
            if (localRoot && physics) localVel = physics->getLinearVelocity(*localRoot);
            m_latestVels[net.getLocalPeerId()] = localVel;
        }

        std::unordered_set<PeerId> rosterIds;
        for (const auto& info : net.getRoster()) {
            if (info.isPlayer) rosterIds.insert(info.id);
        }

        std::vector<std::pair<PeerId, CFrame>> entries;
        for (const auto& [id, pose] : m_latestPoses) {
            if (rosterIds.find(id) == rosterIds.end()) continue;
            entries.emplace_back(id, pose);
            if (entries.size() >= 255) break;
        }
        if (entries.empty()) return;

        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::AvatarBatch));
        w.writeU32(hostTickForSend());
        w.writeU8(static_cast<uint8_t>(entries.size()));
        for (const auto& [id, pose] : entries) {
            auto velIt = m_latestVels.find(id);
            Vector3 vel = velIt != m_latestVels.end() ? velIt->second : Vector3{};
            float walkCycle = 0.0f;
            bool grounded = true;
            bool seated = false;
            bool ragdoll = false;
            if (id == net.getLocalPeerId()) {
                if (m_user && m_user->humanoid) {
                    walkCycle = m_user->humanoid->getWalkCycle();
                    grounded = m_user->humanoid->getIsGrounded();
                    seated = m_user->humanoid->isSeated();
                    ragdoll = m_user->humanoid->isRagdoll();
                }
            } else if (auto avatarIt = m_remoteAvatars.find(id); avatarIt != m_remoteAvatars.end() && avatarIt->second.humanoid) {
                walkCycle = avatarIt->second.humanoid->getWalkCycle();
                grounded = avatarIt->second.humanoid->getIsGrounded();
                seated = avatarIt->second.humanoid->isSeated();
                ragdoll = avatarIt->second.humanoid->isRagdoll();
            }
            w.writeU32(id);
            w.writeVector3(pose.Position);
            w.writeQuat(pose.Rotation);
            w.writeVector3(vel);
            auto seqIt = m_lastProcessedSeq.find(id);
            w.writeU32(seqIt != m_lastProcessedSeq.end() ? seqIt->second : 0u);
            w.writeF32(walkCycle);
            w.writeU8(static_cast<uint8_t>(
                (grounded ? 1 : 0) | (seated ? 2 : 0) |
                (ragdoll ? 4 : 0)));
        }
        net.sendBytes(w.data, NetworkChannel::Unreliable);
        if (m_debugInjectNaNAvatarBatch) sendDebugNaNAvatarBatch(entries);
    }
}

void ReplicationManager::setDebugInjectNaNAvatarBatch(bool enabled) {
    m_debugInjectNaNAvatarBatch = enabled;
    m_debugInjectBatchCounter = 0;
    if (enabled) {
        RCBN_WARN("[DebugInject] NaN AvatarBatch injection is ENABLED (verification only). "
                  "Host sends a NaN copy of every AvatarBatch entry about once per second.");
    }
}

// 検証専用: 通常のAvatarBatchとは別に、全エントリの位置・速度をNaNにしたAvatarBatchを送る。
// 正規の姿勢は直前のAvatarBatchで届いているため、修正済みClientはNaN分だけを捨てて動作を続ける。
void ReplicationManager::sendDebugNaNAvatarBatch(const std::vector<std::pair<PeerId, CFrame>>& entries) {
    constexpr uint32_t INJECT_EVERY_BATCHES = 20; // AvatarBatchは0.05秒間隔なので約1秒ごと
    if (++m_debugInjectBatchCounter % INJECT_EVERY_BATCHES != 0) return;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::AvatarBatch));
    w.writeU32(hostTickForSend());
    w.writeU8(static_cast<uint8_t>(entries.size()));
    for (const auto& [id, pose] : entries) {
        w.writeU32(id);
        w.writeVector3(Vector3(nan, nan, nan));
        w.writeQuat(pose.Rotation);
        w.writeVector3(Vector3(nan, nan, nan));
        w.writeU32(0u);
        w.writeF32(0.0f);
        w.writeU8(1);
    }
    NetworkManager::get().sendBytes(w.data, NetworkChannel::Unreliable);
    ++m_debugInjectSentCount;
    RCBN_LOG("[DebugInject] sent NaN AvatarBatch #" << m_debugInjectSentCount
             << " (entries=" << entries.size() << ")");
}

void ReplicationManager::onGameMessage(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId) {
    const std::uint64_t receivedGeneration = m_workspaceGeneration;
    const auto receivedWorkspace = m_workspace;
    auto bindingIsCurrent = [&]() {
        return receivedGeneration == m_workspaceGeneration &&
               receivedWorkspace == m_workspace;
    };
    if (type == static_cast<uint8_t>(MessageType::AvatarState)) {
        auto& net = NetworkManager::get();
        const auto sender = std::find_if(
            net.getRoster().begin(), net.getRoster().end(),
            [senderId](const PeerInfo& info) {
                return info.id == senderId && info.isPlayer;
            });
        if (net.getRole() != NetworkRole::Host || sender == net.getRoster().end())
            return;
        ByteReader r{payload, len};
        uint8_t count = 0;
        uint32_t firstSeq = 0;
        if (!r.readU8(count) || !r.readU32(firstSeq)) return;
        if (count == 0 || count > MAX_INPUTS_PER_PACKET) return;
        auto flattenAndClamp = [](Vector3 v) {
            v.y = 0.0f;
            float len = v.length();
            return (len > 1.0f) ? (v * (1.0f / len)) : v;
        };
        std::vector<AvatarInputWire> inputs;
        inputs.reserve(count);
        for (uint8_t i = 0; i < count; ++i) {
            AvatarInputWire in;
            uint8_t flags = 0;
            if (!r.readVector3(in.flatForward) || !r.readVector3(in.flatRight) || !r.readVector3(in.targetMoveDir)
                || !r.readU8(flags) || !r.readF32(in.forwardAxis) || !r.readF32(in.rightAxis)
                || !r.readF32(in.smoothing)) return;
            // 破損/悪意ある入力でも歩行速度定数を超えられないよう、1件でも非有限ならパケットごと捨てる
            if (!isFiniteWireVector(in.flatForward) || !isFiniteWireVector(in.flatRight) ||
                !isFiniteWireVector(in.targetMoveDir) || !std::isfinite(in.forwardAxis) ||
                !std::isfinite(in.rightAxis) || !std::isfinite(in.smoothing)) return;
            in.seq = firstSeq + i;
            in.isPressingMove  = (flags & 1) != 0;
            in.ctrlLockEnabled = (flags & 2) != 0;
            in.hasInput        = (flags & 4) != 0;
            in.jump            = (flags & 8) != 0;
            in.standUp         = (flags & 16) != 0;
            in.flatForward = flattenAndClamp(in.flatForward);
            in.flatRight = flattenAndClamp(in.flatRight);
            in.targetMoveDir = in.isPressingMove ? flattenAndClamp(in.targetMoveDir) : Vector3{};
            in.forwardAxis = std::clamp(in.forwardAxis, -1.0f, 1.0f);
            in.rightAxis = std::clamp(in.rightAxis, -1.0f, 1.0f);
            in.smoothing = std::clamp(in.smoothing, 0.0f, 1.0f);
            inputs.push_back(in);
        }
        hostQueueInputs(senderId, inputs);
    } else if (type == static_cast<uint8_t>(MessageType::AvatarBatch)) {
        if (NetworkManager::get().getRole() != NetworkRole::Client) return;
        ByteReader r{payload, len};
        uint32_t hostTick = 0;
        uint8_t count = 0;
        if (!r.readU32(hostTick) || !r.readU8(count)) return;
        clientNoteHostTick(hostTick);

        auto& net = NetworkManager::get();
        PeerId localId = net.getLocalPeerId();

        for (uint8_t i = 0; i < count; ++i) {
            uint32_t id = 0;
            Vector3 pos;
            Quaternion rot;
            Vector3 vel;
            uint32_t lastProcessedSeq = 0;
            float walkCycle = 0.0f;
            uint8_t visualFlags = 0;
            if (!r.readU32(id) || !r.readVector3(pos) || !r.readQuat(rot) || !r.readVector3(vel)
                || !r.readU32(lastProcessedSeq) || !r.readF32(walkCycle) || !r.readU8(visualFlags)) return;
            if (!isFiniteWireVector(pos) || !isFiniteWireVector(vel)) {
                RCBN_WARN("Replication: AvatarBatch entry with non-finite pose/velocity ignored (id=" << id << ")");
                continue;
            }
            if (id == localId) {
                if (m_user && m_user->humanoid) {
                    m_user->humanoid->setRagdollStateForReplication(
                        (visualFlags & 4) != 0, m_physics);
                }
                m_hostAuthoritativeSelfPose = CFrame(pos, rot);
                m_hasHostAuthoritativeSelfPose = true;
                m_hostAckedSeq = lastProcessedSeq;
                m_hostAuthoritativeSelfVel = vel;
                continue;
            }
            m_latestPoses[id] = CFrame(pos, rot);
            if (auto avatarIt = m_remoteAvatars.find(id); avatarIt != m_remoteAvatars.end()) {
                pushSnapshot(avatarIt->second.snapshots, hostTick, CFrame(pos, rot));
                avatarIt->second.walkCycle = walkCycle;
                avatarIt->second.grounded = (visualFlags & 1) != 0;
                avatarIt->second.seated = (visualFlags & 2) != 0;
                avatarIt->second.ragdoll = (visualFlags & 4) != 0;
                if (avatarIt->second.humanoid) {
                    avatarIt->second.humanoid->setRagdollStateForReplication(
                        avatarIt->second.ragdoll, m_physics);
                }
            }
            // 他ピア分の速度は読み捨て(表示はRoot姿勢の平滑補間を使う)
        }
    } else if (type == static_cast<uint8_t>(MessageType::WorldMapping)) {
        if (bindingIsCurrent() &&
            NetworkManager::get().getRole() == NetworkRole::Client) {
            clientApplyWorldMapping(payload, len);
        }
    } else if (type == static_cast<uint8_t>(MessageType::WorldTransforms)) {
        if (bindingIsCurrent() &&
            NetworkManager::get().getRole() == NetworkRole::Client) {
            clientStoreWorldTransforms(payload, len);
        }
    } else if (type == static_cast<uint8_t>(MessageType::SeatState)) {
        if (bindingIsCurrent() &&
            NetworkManager::get().getRole() == NetworkRole::Client) {
            clientApplySeatState(payload, len);
        }
    } else if (type == static_cast<uint8_t>(MessageType::SimulationClock)) {
        Physics* receivedPhysics = m_physics;
        if (!bindingIsCurrent() ||
            NetworkManager::get().getRole() != NetworkRole::Client ||
            !receivedPhysics)
            return;
        ByteReader reader{payload, len};
        std::uint32_t tick = 0;
        float alpha = 0.0f;
        if (!reader.readU32(tick) || !reader.readF32(alpha) ||
            !std::isfinite(alpha))
            return;
        const double phase = static_cast<double>(tick) +
            static_cast<double>(alpha) +
            static_cast<double>(NetworkManager::get().getHostPeerRttMs()) *
                0.001 * 60.0 * 0.5;
        const double whole = std::floor(phase);
        const auto adjustedTick = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(whole) & 0xffffffffu);
        if (!bindingIsCurrent() || receivedPhysics != m_physics) return;
        receivedPhysics->synchronizeSimulationClock(
            adjustedTick, static_cast<float>(phase - whole));
    }
}

void ReplicationManager::reconcileAvatars() {
    auto& net = NetworkManager::get();
    PeerId localId = net.getLocalPeerId();

    for (const auto& info : net.getRoster()) {
        if (info.id == localId || !info.isPlayer) continue;
        if (m_remoteAvatars.find(info.id) == m_remoteAvatars.end()) {
            spawnRemoteAvatar(info.id);
        }
    }

    std::unordered_set<PeerId> rosterIds;
    for (const auto& info : net.getRoster()) {
        if (info.isPlayer) rosterIds.insert(info.id);
    }

    std::vector<PeerId> toRemove;
    for (const auto& [id, avatar] : m_remoteAvatars) {
        if (rosterIds.find(id) == rosterIds.end()) toRemove.push_back(id);
    }
    for (PeerId id : toRemove) despawnRemoteAvatar(id);
}

static void collectBaseCubes(const std::shared_ptr<Instance>& node, std::vector<BaseCube*>& out) {
    auto cube = std::dynamic_pointer_cast<BaseCube>(node);
    if (cube) out.push_back(cube.get());
    for (auto& [name, child] : node->getChildren()) {
        collectBaseCubes(child, out);
    }
}

static void collectSyncTargets(const std::shared_ptr<Instance>& node, const std::unordered_set<Instance*>& excludedCharacters,
                                std::vector<std::shared_ptr<BaseCube>>& out) {
    if (excludedCharacters.contains(node.get())) return;

    auto cube = std::dynamic_pointer_cast<BaseCube>(node);
    if (cube && !cube->Anchored && cube->CanCollide) out.push_back(cube);

    for (auto& [name, child] : node->getChildren()) {
        collectSyncTargets(child, excludedCharacters, out);
    }
}

void ReplicationManager::enablePhysicsProxy(RemoteAvatar& avatar, PeerId id, Physics* physics) {
    if (avatar.isPhysicsProxy || !avatar.humanoid || !physics) return;

    auto root = avatar.humanoid->getRootPart();
    if (!root) return;
    root->CanCollide = true;
    root->Anchored = false;
    physics->recreateActor(root);
    avatar.isPhysicsProxy = true;

    // ローカルキャラと同じリグ構成で動かすため、Root以外はテンプレートのCanCollideへ戻す
    // (表示専用アバターとして非衝突化していたもの)。setCanCollideはフィルタ更新のみで
    // ボディ・拘束を作り直さない
    for (const auto& [part, rel] : avatar.parts) {
        (void)rel;
        if (!part || part == root.get()) continue;
        const auto found = avatar.templateCanCollide.find(part);
        if (found != avatar.templateCanCollide.end() && found->second)
            part->setCanCollide(true);
    }

    // 既に物理参加中の全キャラ(ローカルUser + 他の物理プロキシ)の衝突ボディ同士の衝突を除外する
    // (プレイヤー同士のPvP衝突は今回スコープ外のため)
    auto collectCollidingBodies = [](Instance* character) {
        std::vector<std::shared_ptr<BaseCube>> bodies;
        for (auto& body : CharacterRig::collectR6Bodies(character)) {
            if (body && body->CanCollide) bodies.push_back(std::move(body));
        }
        return bodies;
    };
    const auto ownBodies = collectCollidingBodies(avatar.model.get());
    std::vector<std::shared_ptr<BaseCube>> otherBodies;
    if (m_user && m_user->character) {
        for (auto& body : collectCollidingBodies(m_user->character.get()))
            otherBodies.push_back(std::move(body));
    }
    for (auto& [otherId, otherAvatar] : m_remoteAvatars) {
        if (otherId == id) continue;
        if (otherAvatar.isPhysicsProxy && otherAvatar.model) {
            for (auto& body : collectCollidingBodies(otherAvatar.model.get()))
                otherBodies.push_back(std::move(body));
        }
    }
    for (const auto& ownBody : ownBodies) {
        for (const auto& otherBody : otherBodies) {
            auto nc = std::make_shared<NoCollision>(ownBody, otherBody);
            avatar.model->addChild(nc);
        }
    }

    RCBN_LOG("Replication: enabled physics proxy for peer " << id);
}

bool ReplicationManager::hasPendingPhysicsRegistration(const RemoteAvatar& avatar) const {
    if (!avatar.model || !m_workspace) return false;
    auto belongsToAvatar = [&](const std::shared_ptr<Instance>& pending) {
        for (auto current = pending; current; current = current->Parent.lock()) {
            if (current.get() == avatar.model.get()) return true;
        }
        return false;
    };
    return std::any_of(m_workspace->pendingInstances.begin(), m_workspace->pendingInstances.end(), belongsToAvatar)
        || std::any_of(m_workspace->pendingConstraints.begin(), m_workspace->pendingConstraints.end(), belongsToAvatar);
}

void ReplicationManager::spawnRemoteAvatar(PeerId id) {
    const std::string characterName = NetworkIdentity::characterName(id);
    const std::string identityName = NetworkIdentity::userName(id);
    if (m_workspace->children.contains(characterName)) {
        RCBN_ERROR("Replication: canonical character collision for " << characterName);
        m_fatalIdentityError = true;
        return;
    }
    std::shared_ptr<Instance> users;
    if (m_characterSearchRoot) {
        auto usersIt = m_characterSearchRoot->children.find("Users");
        if (usersIt != m_characterSearchRoot->children.end()) users = usersIt->second;
    }
    if (!users || users->children.contains(identityName)) {
        RCBN_ERROR("Replication: Users missing or canonical identity collision for " << identityName);
        m_fatalIdentityError = true;
        return;
    }
    auto model = User::buildCharacterModel(m_characterSearchRoot, characterName);
    if (!model) {
        RCBN_LOG("Replication: failed to build remote avatar model for peer " << id);
        return;
    }

    std::shared_ptr<Humanoid> humanoid;
    if (auto it = model->getChildren().find("Humanoid"); it != model->getChildren().end()) {
        humanoid = std::dynamic_pointer_cast<Humanoid>(it->second);
        if (humanoid) {
            humanoid->resolveParts(model.get());
            User::placeCharacterAtSpawn(model, humanoid, m_workspace.get(), id);
        }
    }

    std::vector<BaseCube*> cubes;
    for (auto& [name, child] : model->getChildren()) {
        collectBaseCubes(child, cubes);
    }
    std::unordered_map<BaseCube*, bool> templateCanCollide;
    for (BaseCube* cube : cubes) {
        templateCanCollide[cube] = cube->CanCollide;
        cube->CanCollide = false;
    }

    BaseCube* root = nullptr;
    for (BaseCube* cube : cubes) {
        if (cube->Name == "Root") { root = cube; break; }
    }
    if (!root) {
        RCBN_LOG("Replication: remote avatar model has no Root for peer " << id);
        return;
    }

    RemoteAvatar avatar;
    avatar.model = model;
    avatar.humanoid = humanoid;
    avatar.templateCanCollide = std::move(templateCanCollide);
    const CFrame rootWorldInverse = root->getWorldCFrame().inverse();
    for (BaseCube* cube : cubes) {
        avatar.parts.emplace_back(
            cube, rootWorldInverse * cube->getWorldCFrame());
    }

    auto identity = User::createRemoteUser(id);
    identity->characterSmoothing = m_user ? m_user->characterSmoothing : 0.15f;
    identity->character = model;
    model->lockRuntimeName();
    users->addChild(identity);
    avatar.identity = identity;
    m_workspace->addChild(model);
    m_remoteAvatars[id] = std::move(avatar);

    RCBN_LOG("Replication: spawned " << characterName);
}

void ReplicationManager::despawnRemoteAvatar(PeerId id) {
    auto it = m_remoteAvatars.find(id);
    if (it == m_remoteAvatars.end()) return;

    m_workspace->removeChild(it->second.model->Name);
    if (it->second.identity) {
        if (auto parent = it->second.identity->Parent.lock()) parent->removeChild(it->second.identity->Name);
    }
    if (it->second.humanoid) it->second.humanoid->setControlStepCallback({});
    m_latestPoses.erase(id);
    m_latestVels.erase(id);
    m_inputQueues.erase(id);
    m_lastProcessedSeq.erase(id);
    m_remoteAvatars.erase(it);

    RCBN_LOG("Replication: despawned " << NetworkIdentity::characterName(id));
}

void ReplicationManager::despawnAllAvatars() {
    std::vector<PeerId> ids;
    for (const auto& [id, avatar] : m_remoteAvatars) ids.push_back(id);
    for (PeerId id : ids) despawnRemoteAvatar(id);
}

void ReplicationManager::applyAvatarPoses(float dt) {
    for (auto& [id, avatar] : m_remoteAvatars) {
        if (avatar.isPhysicsProxy) continue; // Humanoid::move()内のapplyBodyAnimation()が既に全パーツを駆動済み
        // 初期Weldのoffsetはテンプレート姿勢のまま確定させる。受信姿勢や歩行アニメを
        // pendingConstraintsのflush前に適用すると、Headだけが動いた状態を焼き付けてしまう。
        if (hasPendingPhysicsRegistration(avatar)) continue;
        if (avatar.humanoid && avatar.humanoid->isRagdoll()) continue;
        auto it = m_latestPoses.find(id);
        if (it == m_latestPoses.end()) continue;
        const CFrame& pose = it->second;

        // Host tick基準で「少し前」の姿勢を前後の受信姿勢から補間する。
        // 時刻がまだ決まっていなければ最新の受信姿勢をそのまま使う
        CFrame displayed = pose;
        if (m_hasRenderTick) sampleSnapshots(avatar.snapshots, m_renderTick, displayed);
        if (!avatar.hasPose) {
            avatar.hasPose = true;
            RCBN_LOG("Replication: first pose for peer " << id);
        }
        avatar.current = displayed;

        // Client側のアバターは表示専用。毎フレーム姿勢を与えるので、重力で沈ませない
        if (!avatar.gravityDisabled && m_physics &&
            NetworkManager::get().getRole() == NetworkRole::Client) {
            for (const auto& [part, rel] : avatar.parts) {
                (void)rel;
                if (part && m_physics->hasBody(*part)) m_physics->setGravityEnabled(*part, false);
            }
            avatar.gravityDisabled = true;
        }

        BaseCube* root = nullptr;
        for (const auto& [part, rel] : avatar.parts) {
            (void)rel;
            if (part && part->Name == "Root") {
                root = part;
                break;
            }
        }
        if (!root) {
            RCBN_ERROR("Replication: remote avatar " << id
                       << " has no Root while applying pose");
            continue;
        }

        // A rig may contain several logical members backed by one native
        // compound body (for example Head + Weld accessories).  Applying the
        // root alone leaves the other members at their old world poses, while
        // applying the same transform once per member moves a compound more
        // than once.  Snapshot the current member pose and move each native
        // body exactly once by the same world delta.
        const CFrame currentRoot = m_physics && m_physics->hasBody(*root)
            ? m_physics->getMemberWorldCFrame(*root)
            : root->getWorldCFrame();
        const CFrame worldDelta = avatar.current * currentRoot.inverse();
        std::unordered_set<std::uint64_t> movedBodies;
        std::unordered_set<BaseCube*> movedMembers;
        std::vector<BaseCube*> nativeMembersToSync;
        for (const auto& [part, rel] : avatar.parts) {
            (void)rel;
            if (!part || !movedMembers.insert(part).second) continue;

            if (m_physics && m_physics->hasBody(*part)) {
                const PhysicsBodyHandle handle = m_physics->getBodyHandle(*part);
                if (!handle) {
                    RCBN_ERROR("Replication: remote avatar " << id
                               << " has invalid native body handle for "
                               << part->Name);
                    continue;
                }
                nativeMembersToSync.push_back(part);
                if (movedBodies.insert(handle.value).second) {
                    const CFrame memberPose = m_physics->getMemberWorldCFrame(*part);
                    m_physics->setMemberWorldCFrame(
                        *part, worldDelta * memberPose);
                }
                continue;
            }

            part->setWorldCFrame(worldDelta * part->getWorldCFrame());
        }
        // setMemberWorldCFrame updates the native body. Synchronize its
        // logical members immediately so rendering/constraints observe the
        // new pose before the next physics step. Every member is synchronized,
        // while the native body teleport above remains unique per body.
        for (BaseCube* member : nativeMembersToSync) {
            m_physics->syncCube(*member);
        }
        if (avatar.humanoid && !avatar.humanoid->isRagdoll()) {
            avatar.humanoid->setWalkCycle(avatar.walkCycle);
            avatar.humanoid->setIsGroundedForReplication(avatar.grounded);
            avatar.humanoid->setSeatedForReplication(avatar.seated);
            avatar.humanoid->applyBodyAnimation(false, false, dt);
        }
    }
}

void ReplicationManager::reconcileLocalPose() {
    if (!m_hasHostAuthoritativeSelfPose) return;
    m_hasHostAuthoritativeSelfPose = false; // 1回消費
    const uint32_t ackSeq = m_hostAckedSeq;

    // Hostはseq Nの入力を自分の1stepで使う直前の状態を返す。Clientの同じseqの
    // 「適用前」状態と比べる(現在の状態と比べると、遅延中の正常な移動までズレと誤認する)
    std::optional<BufferedInput> atAck;
    for (const auto& entry : m_inputHistory) {
        if (entry.seq == ackSeq) {
            atAck = entry;
            break;
        }
    }

    // ack済み(Hostが処理済み)の入力履歴は不要なので破棄する(未ack分だけを再送し続ける)
    while (!m_inputHistory.empty() && m_inputHistory.front().seq <= ackSeq) {
        m_inputHistory.pop_front();
    }

    if (!atAck || !m_user || !m_user->humanoid) return;
    // 着席中はRootがSeat(Hostから同期される乗り物)のcompoundに属するため、自キャラだけ補正しない
    if (m_user->humanoid->isSeated()) return;

    const Vector3 hostPosition = m_hostAuthoritativeSelfPose.Position;
    if (!isFiniteWireVector(hostPosition) || !isFiniteWireVector(atAck->rootPositionBefore)) return;

    const Vector3 error = hostPosition - atAck->rootPositionBefore;
    const float errorLength = error.length();
    if (!std::isfinite(errorLength)) return;

    // 同じ入力を同じstepで使うため、通常の誤差はほぼ0。ずれるのは、Clientでは
    // キネマティックな同期物体・他プレイヤーとの接触や、入力の到着遅れがあったとき
    constexpr float kAcceptedError = 0.25f;
    constexpr float kHardSnapError = 12.0f;
    if (errorLength <= kAcceptedError) return;

    const bool hardSnap = errorLength > kHardSnapError;
    float alpha = 1.0f;
    if (!hardSnap) {
        alpha = 0.5f / errorLength;
        if (alpha > 0.25f) alpha = 0.25f; // 1回の可視補正は最大0.5stud
    }
    const Vector3 offset = error * alpha;
    // 速度の差(ジャンプの成否が食い違った等)は位置だけでは追いつかないため、そのまま合わせる
    Vector3 velocityDelta = m_hostAuthoritativeSelfVel - atAck->rootVelocityBefore;
    if (!isFiniteWireVector(velocityDelta)) velocityDelta = Vector3{};
    translateLocalCharacter(offset, velocityDelta);

    // 補正ぶんは未ack履歴にも反映し、次回の比較で二重に数えない
    for (auto& entry : m_inputHistory) {
        entry.rootPositionBefore = entry.rootPositionBefore + offset;
        entry.rootVelocityBefore = entry.rootVelocityBefore + velocityDelta;
    }

    RCBN_LOG("Replication: " << (hardSnap ? "hard" : "soft")
             << " correction (error=" << errorLength << ", ack=" << ackSeq << ")");
}

void ReplicationManager::translateLocalCharacter(const Vector3& offset, const Vector3& velocityDelta) {
    if (!m_user || !m_user->character || !m_physics) return;

    // Motor6Dで繋がった手足は別ボディなので、Rootだけ動かすとばねで引き戻されて揺れる。
    // 全リグボディを同じ並進で動かし、同じネイティブボディは一度だけ動かす。
    // setMemberWorldCFrameは速度を0にするため、ジャンプ中の上昇速度などを保存して戻す
    const auto bodies = CharacterRig::collectR6Bodies(m_user->character.get());
    std::unordered_set<std::uint64_t> movedBodies;
    for (const auto& body : bodies) {
        if (!body || !m_physics->hasBody(*body)) continue;
        const PhysicsBodyHandle handle = m_physics->getBodyHandle(*body);
        if (!handle || !movedBodies.insert(handle.value).second) continue;

        const Vector3 linearVelocity = m_physics->getLinearVelocity(*body);
        const Vector3 angularVelocity = m_physics->getAngularVelocity(*body);
        CFrame target = m_physics->getMemberWorldCFrame(*body);
        target.Position = target.Position + offset;
        m_physics->setMemberWorldCFrame(*body, target);
        m_physics->setLinearVelocity(*body, linearVelocity + velocityDelta);
        m_physics->setAngularVelocity(*body, angularVelocity);
    }
    for (const auto& body : bodies) {
        if (body && m_physics->hasBody(*body)) m_physics->syncCube(*body);
    }
}

void ReplicationManager::hostSimulateAvatars(float dt, Physics* physics) {
    (void)dt;
    if (!physics) return;

    for (auto& [id, avatar] : m_remoteAvatars) {
        if (!avatar.humanoid) continue;
        auto root = avatar.humanoid->getRootPart();
        if (!root) continue;

        if (!avatar.isPhysicsProxy) {
            // Model追加で登録されたCube/WeldをPhysics::update()が初期姿勢のまま
            // 取り込み終えるまでは、rootの再構築も代理move()も行わない。
            if (hasPendingPhysicsRegistration(avatar)) continue;
            enablePhysicsProxy(avatar, id, physics);
            // proxy再構築直後も1フレームは代理移動を保留する。
            continue;
        }

        // ローカルのUserと同じく、移動・接地判定は固定stepごとにHumanoid::stepControlで行う。
        // 各stepの冒頭で、そのピアの入力バッファから次のseqを1つだけ供給する
        avatar.humanoid->setTickControlled(true);
        if (!avatar.inputCallbackInstalled) {
            const PeerId peer = id;
            avatar.humanoid->setControlStepCallback([this, peer](Humanoid& humanoid) {
                hostConsumeInputStep(peer, humanoid);
            });
            avatar.inputCallbackInstalled = true;
        }
    }
}

void ReplicationManager::hostQueueInputs(PeerId id, const std::vector<AvatarInputWire>& inputs) {
    PeerInputQueue& queue = m_inputQueues[id];
    uint32_t processed = 0;
    if (auto it = m_lastProcessedSeq.find(id); it != m_lastProcessedSeq.end()) processed = it->second;
    for (const auto& in : inputs) {
        if (in.seq <= processed) continue;                    // 処理済み(再送分)
        if (queue.started && in.seq < queue.cursor) continue; // 読み飛ばし済み
        queue.inputs.emplace(in.seq, in);                     // 同じseqの再送は最初の1件だけ
    }
}

void ReplicationManager::hostConsumeInputStep(PeerId id, Humanoid& humanoid) {
    PeerInputQueue& queue = m_inputQueues[id];
    if (!queue.started) {
        // 到着の揺れを吸収するため、少し溜まってから消費を始める
        if (queue.inputs.size() < INPUT_BUFFER_START) {
            humanoid.clearControlInput();
            return;
        }
        queue.started = true;
        queue.cursor = queue.inputs.begin()->first;
    }
    if (queue.inputs.size() > INPUT_BUFFER_MAX) {
        // 溜まりすぎ(Clientの方が速い・一時停止明け等)。古い入力を捨てて遅延を戻す
        const size_t dropped = queue.inputs.size() - INPUT_BUFFER_START;
        while (queue.inputs.size() > INPUT_BUFFER_START) queue.inputs.erase(queue.inputs.begin());
        queue.cursor = queue.inputs.begin()->first;
        RCBN_LOG("Replication: input buffer of peer " << id << " overflowed; skipped "
                 << dropped << " step(s)");
    }

    const auto next = queue.inputs.find(queue.cursor);
    if (next == queue.inputs.end()) {
        // まだ届いていない。直前の移動だけ続け、読み取り位置は進めない
        // (遅れた分だけバッファが伸び、以降の到着の揺れに耐えられるようになる)
        if (queue.hasLast) applyMovementInput(humanoid, queue.last);
        else humanoid.clearControlInput();
        return;
    }

    const AvatarInputWire input = next->second;
    applyMovementInput(humanoid, input);
    if (input.standUp) humanoid.requestStandUp();
    if (input.jump) humanoid.requestJump();

    // このseqを使う直前の状態を、Clientの同じseqの「適用前」状態と比べられるよう返す
    if (auto root = humanoid.getRootPart()) {
        m_latestPoses[id] = root->getWorldCFrame();
        m_latestVels[id] = m_physics ? m_physics->getLinearVelocity(*root) : Vector3{};
    }
    m_lastProcessedSeq[id] = input.seq;

    queue.last = input;
    queue.last.jump = false;
    queue.last.standUp = false;
    queue.hasLast = true;
    queue.inputs.erase(next);
    ++queue.cursor;
}

void ReplicationManager::hostUpdateWorld(float dt) {
    auto& net = NetworkManager::get();

    m_worldRescanTimer += dt;
    if (m_worldRescanTimer >= 1.0f) {
        m_worldRescanTimer = 0.0f;
        hostRescanWorld();
    }

    // ロスターが増えたら(新規Client接続)マッピングを再配布する
    size_t rosterSize = net.getRoster().size();
    if (rosterSize > m_prevRosterSize) m_worldMappingDirty = true;
    m_prevRosterSize = rosterSize;

    if (m_worldMappingDirty && net.hasPeers()) {
        hostBroadcastMapping();
        m_worldMappingDirty = false;
    }

    m_worldSnapshotTimer += dt;
    if (m_worldSnapshotTimer >= 5.0f) {
        m_worldSnapshotTimer = 0.0f;
        m_worldSnapshotPending = true;
    }

    m_worldSendTimer += dt;
    if (m_worldSendTimer >= 0.05f) {
        m_worldSendTimer = 0.0f;
        if (net.hasPeers()) {
            hostSendWorldTransforms(m_worldSnapshotPending);
            m_worldSnapshotPending = false;
        }
    }

    const float elapsed = std::max(dt, 0.0f);
    for (auto& [id, obj] : m_hostObjects) {
        (void)id;
        obj.tailTimer = std::max(0.0f, obj.tailTimer - elapsed);
    }
}

void ReplicationManager::hostRescanWorld() {
    std::unordered_set<Instance*> excludedCharacters;
    if (m_user && m_user->character) excludedCharacters.insert(m_user->character.get());
    for (auto& [peerId, avatar] : m_remoteAvatars) {
        if (avatar.model) excludedCharacters.insert(avatar.model.get());
    }

    std::vector<std::shared_ptr<BaseCube>> targets;
    for (auto& [name, child] : m_workspace->getChildren()) {
        collectSyncTargets(child, excludedCharacters, targets);
    }

    std::unordered_set<std::string> currentPaths;
    for (auto& target : targets) {
        std::string path = target->getWorkspaceRelativePath();
        currentPaths.insert(path);
        if (m_pathToNetId.find(path) == m_pathToNetId.end()) {
            uint32_t id = m_nextNetId++;
            m_pathToNetId[path] = id;
            m_worldMappingDirty = true;
        }
    }

    // 今回の走査で使われなかった旧パスは削除
    std::vector<std::string> stalePaths;
    for (auto& [path, id] : m_pathToNetId) {
        if (currentPaths.find(path) == currentPaths.end()) stalePaths.push_back(path);
    }
    for (auto& path : stalePaths) {
        m_pathToNetId.erase(path);
        m_worldMappingDirty = true;
    }

    std::unordered_map<uint32_t, HostSyncedObject> newObjects;
    for (auto& target : targets) {
        std::string path = target->getWorkspaceRelativePath();
        auto pathIt = m_pathToNetId.find(path);
        if (pathIt == m_pathToNetId.end()) continue;
        uint32_t id = pathIt->second;

        auto existing = m_hostObjects.find(id);
        if (existing != m_hostObjects.end()) {
            newObjects[id] = existing->second;
        } else {
            HostSyncedObject obj;
            obj.cube = target;
            newObjects[id] = obj;
        }
    }
    m_hostObjects = std::move(newObjects);

    if (m_worldMappingDirty) {
        RCBN_LOG("Replication: world scan found " << m_hostObjects.size() << " synced objects");
    }
}

void ReplicationManager::hostBroadcastMapping() {
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::WorldMapping));
    w.writeU32(static_cast<uint32_t>(m_pathToNetId.size()));
    for (const auto& [path, id] : m_pathToNetId) {
        w.writeU32(id);
        w.writeU16(static_cast<uint16_t>(path.size()));
        w.data.insert(w.data.end(), path.begin(), path.end());
    }
    NetworkManager::get().sendBytes(w.data, NetworkChannel::Reliable);
    RCBN_LOG("Replication: world mapping broadcast " << m_pathToNetId.size() << " objects");
}

void ReplicationManager::hostSendWorldTransforms(bool forceAll) {
    std::vector<std::pair<uint32_t, CFrame>> toSend;
    for (auto& [id, obj] : m_hostObjects) {
        auto cube = obj.cube.lock();
        if (!cube) continue;

        // 受信側はsetWorldCFrameで適用するため、ワールド姿勢を送る(Model配下でずれないように)
        const CFrame cf = cube->getWorldCFrame();
        bool moved = false;
        if (obj.hasSent) {
            Vector3 dp = cf.Position - obj.lastSent.Position;
            float distSq = dp.x*dp.x + dp.y*dp.y + dp.z*dp.z;
            float rdot = cf.Rotation.w*obj.lastSent.Rotation.w + cf.Rotation.x*obj.lastSent.Rotation.x
                       + cf.Rotation.y*obj.lastSent.Rotation.y + cf.Rotation.z*obj.lastSent.Rotation.z;
            if (rdot < 0.0f) rdot = -rdot;
            moved = distSq > 1e-6f || rdot < 0.99999f;
        }
        if (moved) obj.tailTimer = 0.5f;

        bool shouldSend = forceAll || !obj.hasSent || moved || obj.tailTimer > 0.0f;

        if (!shouldSend) continue;

        toSend.emplace_back(id, cf);
        obj.lastSent = cf;
        obj.hasSent  = true;
    }

    if (toSend.empty()) return;

    auto& net = NetworkManager::get();
    constexpr size_t CHUNK = 36;
    for (size_t offset = 0; offset < toSend.size(); offset += CHUNK) {
        size_t count = (std::min)(CHUNK, toSend.size() - offset);
        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::WorldTransforms));
        w.writeU32(hostTickForSend());
        w.writeU16(static_cast<uint16_t>(count));
        for (size_t i = 0; i < count; ++i) {
            const auto& [id, cf] = toSend[offset + i];
            w.writeU32(id);
            w.writeVector3(cf.Position);
            w.writeQuat(cf.Rotation);
        }
        net.sendBytes(w.data, NetworkChannel::Unreliable);
    }
}

void ReplicationManager::clientApplyWorldMapping(const uint8_t* payload, size_t len) {
    ByteReader r{payload, len};
    uint32_t count = 0;
    if (!r.readU32(count)) return;

    std::unordered_map<uint32_t, std::string> newTable;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        uint16_t pathLen = 0;
        if (!r.readU32(id) || !r.readU16(pathLen)) return;
        if (r.remaining < pathLen) return;
        std::string path(reinterpret_cast<const char*>(r.p), pathLen);
        r.p += pathLen;
        r.remaining -= pathLen;
        newTable[id] = std::move(path);
    }

    // 旧マッピングにあって新テーブルに無いnetIdは解放
    std::vector<uint32_t> toRemove;
    for (auto& [id, obj] : m_clientObjects) {
        if (newTable.find(id) == newTable.end()) toRemove.push_back(id);
    }
    for (uint32_t id : toRemove) {
        auto it = m_clientObjects.find(id);
        if (it != m_clientObjects.end()) {
            auto cube = it->second.cube.lock();
            if (cube) cube->setAnchored(false);
            m_clientObjects.erase(it);
        }
    }

    int resolved = 0, unresolved = 0;
    for (auto& [id, path] : newTable) {
        if (m_clientObjects.find(id) != m_clientObjects.end()) { resolved++; continue; }

        Instance* found = m_workspace->getChildByPath(path);

        if (!found) { unresolved++; continue; }
        auto cube = std::dynamic_pointer_cast<BaseCube>(found->shared_from_this());
        if (!cube) { unresolved++; continue; }

        cube->setAnchored(true);
        ClientSyncedObject obj;
        obj.cube = cube;
        m_clientObjects[id] = obj;
        resolved++;
    }

    RCBN_LOG("Replication: world mapping applied " << resolved << " objects (" << unresolved << " unresolved)");
}

void ReplicationManager::clientStoreWorldTransforms(const uint8_t* payload, size_t len) {
    ByteReader r{payload, len};
    uint32_t hostTick = 0;
    uint16_t count = 0;
    if (!r.readU32(hostTick) || !r.readU16(count)) return;
    clientNoteHostTick(hostTick);

    for (uint16_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        Vector3 pos;
        Quaternion rot;
        if (!r.readU32(id) || !r.readVector3(pos) || !r.readQuat(rot)) return;
        if (!isFiniteWireVector(pos)) {
            RCBN_WARN("Replication: WorldTransforms entry with non-finite position ignored (id=" << id << ")");
            continue;
        }

        auto it = m_clientObjects.find(id);
        if (it == m_clientObjects.end()) continue; // マッピング未着のUNRELIABLE先行は正常系

        it->second.target = CFrame(pos, rot);
        it->second.hasTarget = true;
        pushSnapshot(it->second.snapshots, hostTick, it->second.target);
    }
}

void ReplicationManager::clientApplyWorldSmoothing(float dt) {
    // Weldで1つのnative bodyにまとまった同期物体(車体+Seatなど)は、memberの論理姿勢を
    // 書き換えても物理同期でbody poseに戻される。native bodyごとに1回だけ移動目標を渡す
    std::unordered_set<std::uint64_t> drivenBodies;
    for (auto& [id, obj] : m_clientObjects) {
        if (!obj.hasTarget) continue;
        auto cube = obj.cube.lock();
        if (!cube) continue;

        // Host tick基準で「少し前」の姿勢を前後の受信姿勢から補間する
        CFrame displayed = obj.target;
        if (m_hasRenderTick) sampleSnapshots(obj.snapshots, m_renderTick, displayed);

        cube->setWorldCFrame(displayed);
        if (m_physics && cube->Anchored && m_physics->hasBody(*cube)) {
            const PhysicsBodyHandle handle = m_physics->getBodyHandle(*cube);
            if (handle && drivenBodies.insert(handle.value).second)
                m_physics->setMemberKinematicTarget(*cube, displayed);
        }
    }
}

uint32_t ReplicationManager::hostTickForSend() const {
    return m_physics ? static_cast<uint32_t>(m_physics->getSimulationTick() & 0xffffffffu) : 0u;
}

float ReplicationManager::interpolationDelaySeconds() const {
    constexpr float DEFAULT_DELAY = 0.1f;
    const auto* system = dynamic_cast<const System*>(m_characterSearchRoot);
    if (!system || !std::isfinite(system->NetworkInterpolationDelay)) return DEFAULT_DELAY;
    return std::clamp(system->NetworkInterpolationDelay, 0.0f, 1.0f);
}

void ReplicationManager::clientNoteHostTick(uint32_t tick) {
    // 32bitの周回を考慮して新旧を比べる
    if (m_hasHostTick && static_cast<int32_t>(tick - m_latestHostTick) <= 0) return;
    m_latestHostTick = tick;
    m_hasHostTick = true;
    m_ticksSinceHostTick = 0.0;
}

void ReplicationManager::clientAdvanceRenderClock(float dt) {
    if (!m_hasHostTick) return;
    constexpr double TICKS_PER_SECOND = 60.0;
    constexpr double SNAP_ERROR_TICKS = 30.0;   // これ以上ずれたら(初回・長い途絶)合わせ直す
    constexpr double FOLLOW_RATE = 2.0;         // 1秒あたりに詰めるずれの割合
    const double step = static_cast<double>((std::max)(dt, 0.0f)) * TICKS_PER_SECOND;
    m_ticksSinceHostTick += step;
    // 最新受信tickからの経過を足してHostの現在tickを見積もる(受信間隔の段差で時刻が跳ねないように)
    const double target = static_cast<double>(m_latestHostTick) + m_ticksSinceHostTick -
        static_cast<double>(interpolationDelaySeconds()) * TICKS_PER_SECOND;
    if (!m_hasRenderTick || std::abs(target - m_renderTick) > SNAP_ERROR_TICKS) {
        m_renderTick = target;
        m_hasRenderTick = true;
        return;
    }
    m_renderTick += step;
    m_renderTick += (target - m_renderTick) *
        (std::min)(1.0, static_cast<double>((std::max)(dt, 0.0f)) * FOLLOW_RATE);
}

void ReplicationManager::pushSnapshot(
    std::deque<TimedPose>& snapshots, uint32_t tick, const CFrame& pose) {
    constexpr size_t MAX_SNAPSHOTS = 64;
    if (!snapshots.empty() && static_cast<int32_t>(tick - snapshots.back().tick) <= 0) return;
    snapshots.push_back({tick, pose});
    while (snapshots.size() > MAX_SNAPSHOTS) snapshots.pop_front();
}

bool ReplicationManager::sampleSnapshots(
    std::deque<TimedPose>& snapshots, double tick, CFrame& out) {
    if (snapshots.empty()) return false;
    // tickより前の姿勢は、補間の始点になる1件だけ残す
    while (snapshots.size() >= 2 && static_cast<double>(snapshots[1].tick) <= tick)
        snapshots.pop_front();

    // 1件だけ残った(tickが最新より後)か、tickが最古より前なら、その端の姿勢をそのまま使う
    const TimedPose& first = snapshots.front();
    if (snapshots.size() == 1 || tick <= static_cast<double>(first.tick)) {
        out = first.pose;
        return true;
    }
    const TimedPose& second = snapshots[1];
    const double span = static_cast<double>(second.tick) - static_cast<double>(first.tick);
    const float alpha = span > 0.0
        ? static_cast<float>(std::clamp((tick - static_cast<double>(first.tick)) / span, 0.0, 1.0))
        : 1.0f;
    out.Position = first.pose.Position + (second.pose.Position - first.pose.Position) * alpha;
    out.Rotation = Quaternion::Slerp(first.pose.Rotation, second.pose.Rotation, alpha);
    return true;
}

void ReplicationManager::clientReleaseWorldObjects() {
    size_t n = 0;
    for (auto& [id, obj] : m_clientObjects) {
        auto cube = obj.cube.lock();
        if (cube) { cube->setAnchored(false); n++; }
    }
    m_clientObjects.clear();
    RCBN_LOG("Replication: released " << n << " world objects (anchored -> dynamic)");
}

void ReplicationManager::onNetworkRoleChanged(NetworkRole oldRole, NetworkRole newRole) {
    m_physics = m_workspace ? m_workspace->getPhysicsEngine() : nullptr;
    if (newRole == NetworkRole::Offline) {
        if (m_physics) m_physics->resetSimulationClockSynchronization();
        m_simulationClockTimer = 0.0f;
        m_simulationClockRosterSize = 0;
        m_forceReliableSimulationClock = false;
        despawnAllAvatars();
        m_latestPoses.clear();
        m_latestVels.clear();
        clientReleaseWorldObjects();
        m_pathToNetId.clear();
        m_hostObjects.clear();
        m_worldMappingDirty = false;
        m_prevRosterSize = 0;
        m_inputQueues.clear();
        m_lastProcessedSeq.clear();
        clientDetachInputRecording();
        m_inputHistory.clear();
        m_hasHostTick = false;
        m_hasRenderTick = false;
        m_sentSeatPaths.clear();
        m_seatStateRosterSize = 0;
    } else if (oldRole == NetworkRole::Client && newRole == NetworkRole::Host) {
        clientDetachInputRecording();
        m_inputHistory.clear();
        if (m_physics) m_physics->makeSimulationClockAuthoritative();
        m_forceReliableSimulationClock = true;
        // ホスト昇格: クライアントとして凍結(Anchored化)していた物理を解放し、自分の世界を権威として再スキャンさせる
        clientReleaseWorldObjects();
        m_worldMappingDirty = true;
        m_worldRescanTimer = 1.0f; // 次のupdateで即再スキャン
        m_pendingProxyUpgradeAll = true;
    } else if (newRole == NetworkRole::Host) {
        if (m_physics) m_physics->makeSimulationClockAuthoritative();
        m_forceReliableSimulationClock = true;
    }
}

void ReplicationManager::clientEnsureInputRecording() {
    const auto humanoid = m_user ? m_user->humanoid : nullptr;
    const auto current = m_recordingHumanoid.lock();
    if (current == humanoid) return;
    if (current) current->setControlStepCallback({});
    // キャラが替わったら旧キャラの履歴は比較に使えない。seqは連番のまま続ける
    m_inputHistory.clear();
    m_recordingHumanoid = humanoid;
    if (humanoid) {
        humanoid->setControlStepCallback([this](Humanoid& value) {
            clientRecordInputStep(value);
        });
    }
}

void ReplicationManager::clientDetachInputRecording() {
    if (const auto current = m_recordingHumanoid.lock())
        current->setControlStepCallback({});
    m_recordingHumanoid.reset();
}

void ReplicationManager::clientRecordInputStep(Humanoid& humanoid) {
    BufferedInput entry;
    entry.seq = m_nextSeq++;
    AvatarInputWire& in = entry.input;
    in.seq = entry.seq;
    if (const auto& control = humanoid.getControlInput()) {
        in.hasInput        = true;
        in.flatForward     = control->flatForward;
        in.flatRight       = control->flatRight;
        in.targetMoveDir   = control->targetMoveDir;
        in.isPressingMove  = control->isPressingMove;
        in.ctrlLockEnabled = control->ctrlLockEnabled;
        in.forwardAxis     = control->forwardAxis;
        in.rightAxis       = control->rightAxis;
        in.smoothing       = control->smoothing;
    }
    in.jump = humanoid.hasPendingJump();
    in.standUp = humanoid.hasPendingStandUp();
    if (const auto root = humanoid.getRootPart()) {
        entry.rootPositionBefore = root->getWorldPosition();
        if (m_physics && m_physics->hasBody(*root))
            entry.rootVelocityBefore = m_physics->getLinearVelocity(*root);
    }
    m_inputHistory.push_back(entry);

    // 直近4秒(240step)を超えたら古いものから破棄する安全弁
    // (ackが来ずに際限なく溜まり続けることを防ぐ。通常は受信のたびにack済み分が破棄される)
    while (m_inputHistory.size() > 240) {
        m_inputHistory.pop_front();
    }
}

void ReplicationManager::clientSendInputs() {
    auto& net = NetworkManager::get();
    if (!net.hasPeers() || m_inputHistory.empty()) return;

    // 履歴に残っている(=未ackの)入力を、新しい側から最大MAX_INPUTS_PER_PACKET件まとめて送る。
    // seqは連番なので先頭seqだけ書く
    const size_t count = (std::min)(m_inputHistory.size(), MAX_INPUTS_PER_PACKET);
    const auto first = m_inputHistory.end() - static_cast<std::ptrdiff_t>(count);
    ByteWriter writer;
    writer.writeU8(static_cast<uint8_t>(MessageType::AvatarState));
    writer.writeU8(static_cast<uint8_t>(count));
    writer.writeU32(first->seq);
    for (auto it = first; it != m_inputHistory.end(); ++it) {
        const AvatarInputWire& in = it->input;
        writer.writeVector3(in.flatForward);
        writer.writeVector3(in.flatRight);
        writer.writeVector3(in.targetMoveDir);
        writer.writeU8(static_cast<uint8_t>(
            (in.isPressingMove ? 1 : 0) | (in.ctrlLockEnabled ? 2 : 0) |
            (in.hasInput ? 4 : 0) | (in.jump ? 8 : 0) | (in.standUp ? 16 : 0)));
        writer.writeF32(in.forwardAxis);
        writer.writeF32(in.rightAxis);
        writer.writeF32(in.smoothing);
    }
    net.sendBytes(writer.data, NetworkChannel::Unreliable);
}
