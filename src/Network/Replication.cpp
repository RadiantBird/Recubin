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
#include <Instances/Attachment.hpp>
#include <Instances/Force.hpp>
#include <Instances/Motor.hpp>
#include <Instances/PhysicsConstraint.hpp>
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

bool isFiniteWireQuat(const Quaternion& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
}

// currentからtargetへdt秒で回るための角速度
Vector3 angularVelocityToward(const Quaternion& current, const Quaternion& target, float dt) {
    if (dt <= 0.0f) return Vector3{};
    const Quaternion delta = target * current.conjugate();
    float w = delta.w, x = delta.x, y = delta.y, z = delta.z;
    if (w < 0.0f) { w = -w; x = -x; y = -y; z = -z; } // 短い方の回転
    const float s = std::sqrt(x * x + y * y + z * z);
    if (s < 1.0e-6f) return Vector3{};
    const float angle = 2.0f * std::atan2(s, w);
    const float k = angle / (s * dt);
    return Vector3(x * k, y * k, z * k);
}

// Humanoidを子に持つモデル(キャラ)の部品か
bool isCharacterPart(const BaseCube& cube) {
    for (auto node = cube.Parent.lock(); node; node = node->Parent.lock()) {
        for (const auto& [name, child] : node->getChildren()) {
            (void)name;
            if (child->IsA("Humanoid")) return true;
        }
    }
    return false;
}

void collectAttachmentLocals(Instance& parent, std::vector<std::pair<Spatial*, CFrame>>& out) {
    for (const auto& [name, child] : parent.getChildren()) {
        (void)name;
        if (!child) continue;
        if (auto* attachment = dynamic_cast<Attachment*>(child.get()))
            out.emplace_back(attachment, attachment->getCFrame());
        collectAttachmentLocals(*child, out);
    }
}

// 同期した姿勢を部品へ書く。Spatial::setWorldCFrameは子孫のワールド姿勢を保つため、
// そのままだとAttachmentが元の位置に取り残され、Motor等の取り付け位置が移動量だけずれていく
// (主導権の切り替えで拘束を作り直したとき、車輪が外れる・軸がずれる)。
// 物理同期と同じく、Attachmentは部品に付いて動くようローカル姿勢を保つ
void setSyncedWorldCFrame(BaseCube& cube, const CFrame& world) {
    std::vector<std::pair<Spatial*, CFrame>> attachmentLocals;
    collectAttachmentLocals(cube, attachmentLocals);
    cube.setWorldCFrame(world);
    if (!attachmentLocals.empty()) Spatial::applyLocalCFrameBatch(attachmentLocals);
}

constexpr float CLIENT_SEND_INTERVAL = 0.05f; // Client: 自キャラ・所有物体の姿勢送信(20Hz)
constexpr size_t TRANSFORMS_PER_PACKET = 36;
}

ReplicationManager::ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot)
    : m_workspace(workspace), m_user(user), m_characterSearchRoot(characterSearchRoot),
      m_physics(workspace ? workspace->getPhysicsEngine() : nullptr) {
}

ReplicationManager::~ReplicationManager() {
    // Client中に止めた接触着席を、ネットワーク終了後のローカルPlayへ持ち越さない
    Seat::setTouchSitEnabled(true);
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
    despawnAllAvatars(); // 主導権を渡していた物体もHostへ戻す
    clientReleaseWorldObjects();
    m_latestPoses.clear();
    m_latestVels.clear();
    m_peerClocks.clear();
    m_ownership.clear();
    m_ownedMotors.clear();
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
    m_hostClock.reset();
    m_localWasSeated = false;
    m_clientSendTimer = 0.0f;

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
    ensurePreStepCallback();

    if (net.getRole() == NetworkRole::Host) {
        const double delayTicks = interpolationDelayTicks();
        for (auto& [id, clock] : m_peerClocks) {
            (void)id;
            clock.advance(dt, delayTicks);
        }
        hostSendSimulationClock(dt, m_physics);
        // 主導権の表をSeatStateより先に送る(同じReliable経路なので、着席時には所有が届いている)
        hostUpdateOwnership();
        hostSendOwnership();
        hostSendJointDrives();
        hostSendSeatStates();
        hostUpdateWorld(dt);
    } else if (net.getRole() == NetworkRole::Client) {
        clientNotifyStandUp();
        m_clientSendTimer += (std::max)(dt, 0.0f);
        if (m_clientSendTimer >= CLIENT_SEND_INTERVAL) {
            m_clientSendTimer = 0.0f;
            clientSendAvatarState();
            clientSendOwnedTransforms();
        }
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

    // Clientの自キャラ姿勢はclientSendAvatarState()が送る
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
        // 他プレイヤーはHost上のプロキシ(受信姿勢へ追従中)の姿勢を配る
        for (const auto& [id, avatar] : m_remoteAvatars) {
            if (!avatar.isPhysicsProxy || !avatar.humanoid || !m_physics) continue;
            const auto root = avatar.humanoid->getRootPart();
            if (!root) continue;
            m_latestPoses[id] = root->getWorldCFrame();
            m_latestVels[id] = m_physics->getLinearVelocity(*root);
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
    if (type == static_cast<uint8_t>(MessageType::AvatarState) ||
        type == static_cast<uint8_t>(MessageType::OwnedTransforms) ||
        type == static_cast<uint8_t>(MessageType::StandUpRequest)) {
        // Client→Host: 送信元がロスター上のプレイヤーのときだけ受け付ける
        auto& net = NetworkManager::get();
        const auto sender = std::find_if(
            net.getRoster().begin(), net.getRoster().end(),
            [senderId](const PeerInfo& info) {
                return info.id == senderId && info.isPlayer;
            });
        if (!bindingIsCurrent() || net.getRole() != NetworkRole::Host || sender == net.getRoster().end())
            return;
        if (type == static_cast<uint8_t>(MessageType::AvatarState)) {
            hostReceiveAvatarState(senderId, payload, len);
        } else if (type == static_cast<uint8_t>(MessageType::OwnedTransforms)) {
            hostReceiveOwnedTransforms(senderId, payload, len);
        } else if (auto avatarIt = m_remoteAvatars.find(senderId);
                   avatarIt != m_remoteAvatars.end() && avatarIt->second.humanoid &&
                   avatarIt->second.humanoid->isSeated() && m_physics) {
            avatarIt->second.humanoid->standUp(m_physics);
        }
    } else if (type == static_cast<uint8_t>(MessageType::AvatarBatch)) {
        if (NetworkManager::get().getRole() != NetworkRole::Client) return;
        ByteReader r{payload, len};
        uint32_t hostTick = 0;
        uint8_t count = 0;
        if (!r.readU32(hostTick) || !r.readU8(count)) return;
        m_hostClock.noteTick(hostTick);

        auto& net = NetworkManager::get();
        PeerId localId = net.getLocalPeerId();

        for (uint8_t i = 0; i < count; ++i) {
            uint32_t id = 0;
            Vector3 pos;
            Quaternion rot;
            Vector3 vel;
            float walkCycle = 0.0f;
            uint8_t visualFlags = 0;
            if (!r.readU32(id) || !r.readVector3(pos) || !r.readQuat(rot) || !r.readVector3(vel)
                || !r.readF32(walkCycle) || !r.readU8(visualFlags)) return;
            if (!isFiniteWireVector(pos) || !isFiniteWireVector(vel)) {
                RCBN_WARN("Replication: AvatarBatch entry with non-finite pose/velocity ignored (id=" << id << ")");
                continue;
            }
            if (id == localId) {
                // 自キャラの姿勢は自分が演算しているので使わない。Ragdoll(Host権威)だけ反映する
                if (m_user && m_user->humanoid) {
                    m_user->humanoid->setRagdollStateForReplication(
                        (visualFlags & 4) != 0, m_physics);
                }
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
    } else if (type == static_cast<uint8_t>(MessageType::Ownership)) {
        if (bindingIsCurrent() &&
            NetworkManager::get().getRole() == NetworkRole::Client) {
            clientApplyOwnership(payload, len);
        }
    } else if (type == static_cast<uint8_t>(MessageType::JointDrive)) {
        if (bindingIsCurrent() &&
            NetworkManager::get().getRole() == NetworkRole::Client) {
            clientApplyJointDrive(payload, len);
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

// 主導権を渡した物体はHost側でAnchoredにしているが、同期対象には残す
static void collectSyncTargets(const std::shared_ptr<Instance>& node, const std::unordered_set<Instance*>& excludedCharacters,
                                const std::unordered_set<BaseCube*>& ownedCubes,
                                std::vector<std::shared_ptr<BaseCube>>& out) {
    if (excludedCharacters.contains(node.get())) return;

    auto cube = std::dynamic_pointer_cast<BaseCube>(node);
    if (cube && cube->CanCollide && (!cube->Anchored || ownedCubes.contains(cube.get()))) out.push_back(cube);

    for (auto& [name, child] : node->getChildren()) {
        collectSyncTargets(child, excludedCharacters, ownedCubes, out);
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
    // 姿勢はClientから受けて速度で追従させるため、キャラ自身の向き制御・力は止める
    // (Humanoidの固定step制御も使わない)
    avatar.humanoid->setTickControlled(false);
    avatar.humanoid->clearControlInput();
    auto disableControllers = [](auto& self, Instance& node) -> void {
        for (const auto& [name, child] : node.getChildren()) {
            (void)name;
            if (child->IsA("Gyro")) std::static_pointer_cast<Gyro>(child)->setEnabled(false);
            else if (child->IsA("Force")) std::static_pointer_cast<Force>(child)->Enabled = false;
            self(self, *child);
        }
    };
    if (avatar.model) disableControllers(disableControllers, *avatar.model);

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
    hostReclaimOwnership(id); // 運転中に抜けた乗り物はHostが演算を引き継ぐ
    m_latestPoses.erase(id);
    m_latestVels.erase(id);
    m_peerClocks.erase(id);
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
        if (m_hostClock.hasRenderTick())
            sampleSnapshots(avatar.snapshots, m_hostClock.renderTick(), displayed);
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

            setSyncedWorldCFrame(*part, worldDelta * part->getWorldCFrame());
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

void ReplicationManager::translateCharacter(
    Instance& character, const Vector3& offset, const Vector3& velocityDelta) {
    if (!m_physics) return;

    // Motor6Dで繋がった手足は別ボディなので、Rootだけ動かすとばねで引き戻されて揺れる。
    // 全リグボディを同じ並進で動かし、同じネイティブボディは一度だけ動かす。
    // setMemberWorldCFrameは速度を0にするため、ジャンプ中の上昇速度などを保存して戻す
    const auto bodies = CharacterRig::collectR6Bodies(&character);
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

        Humanoid& humanoid = *avatar.humanoid;
        // 受信姿勢へ追従している間は重力を切る(stepごとの沈み込みを防ぐ)。
        // 死亡・Ragdoll中はHostの物理に任せるので重力を戻す
        const bool following = !humanoid.isDead() && !humanoid.isRagdoll();
        if (following != avatar.gravityDisabled) {
            for (const auto& body : CharacterRig::collectR6Bodies(avatar.model.get())) {
                if (body && physics->hasBody(*body)) physics->setGravityEnabled(*body, !following);
            }
            avatar.gravityDisabled = following;
        }
        if (humanoid.isRagdoll()) continue;

        // 着席中はClientの操作軸をSeatへ書く(車のスクリプトはHostで動き、この値を読む)
        if (const auto seat = humanoid.getSeat()) {
            seat->Throttle = avatar.forwardAxis;
            seat->Steer = avatar.rightAxis;
        }
        humanoid.setWalkCycle(avatar.walkCycle);
        humanoid.setIsGroundedForReplication(avatar.grounded);
        humanoid.applyBodyAnimation(false, false, dt);
    }
}

void ReplicationManager::hostReceiveAvatarState(PeerId id, const uint8_t* payload, size_t len) {
    ByteReader r{payload, len};
    uint32_t tick = 0;
    Vector3 pos;
    Quaternion rot;
    Vector3 vel;
    float walkCycle = 0.0f;
    uint8_t flags = 0;
    float forwardAxis = 0.0f;
    float rightAxis = 0.0f;
    if (!r.readU32(tick) || !r.readVector3(pos) || !r.readQuat(rot) || !r.readVector3(vel) ||
        !r.readF32(walkCycle) || !r.readU8(flags) || !r.readF32(forwardAxis) || !r.readF32(rightAxis))
        return;
    if (!isFiniteWireVector(pos) || !isFiniteWireQuat(rot) || !isFiniteWireVector(vel) ||
        !std::isfinite(walkCycle) || !std::isfinite(forwardAxis) || !std::isfinite(rightAxis)) {
        RCBN_WARN("Replication: AvatarState with non-finite values ignored (peer=" << id << ")");
        return;
    }
    auto avatarIt = m_remoteAvatars.find(id);
    if (avatarIt == m_remoteAvatars.end()) return;
    RemoteAvatar& avatar = avatarIt->second;

    m_peerClocks[id].noteTick(tick);
    pushSnapshot(avatar.snapshots, tick, CFrame(pos, rot));
    avatar.walkCycle = walkCycle;
    avatar.grounded = (flags & 1) != 0;
    avatar.forwardAxis = std::clamp(forwardAxis, -1.0f, 1.0f);
    avatar.rightAxis = std::clamp(rightAxis, -1.0f, 1.0f);
}

void ReplicationManager::hostStepProxies(Physics& physics, float dt) {
    for (auto& [id, avatar] : m_remoteAvatars) {
        if (!avatar.isPhysicsProxy || !avatar.humanoid || !avatar.model || avatar.snapshots.empty()) continue;
        Humanoid& humanoid = *avatar.humanoid;
        // 着席中は座席(主導権を渡した乗り物)と一緒に動く。死亡・RagdollはHostの物理に任せる
        if (humanoid.isSeated() || humanoid.isDead() || humanoid.isRagdoll()) continue;
        const auto clock = m_peerClocks.find(id);
        if (clock == m_peerClocks.end() || !clock->second.hasStepTick()) continue;
        const auto root = humanoid.getRootPart();
        if (!root || !physics.hasBody(*root)) continue;

        CFrame target;
        sampleSnapshots(avatar.snapshots, clock->second.stepTick(), target);
        const CFrame current = physics.getMemberWorldCFrame(*root);
        const Vector3 offset = target.Position - current.Position;
        if (offset.length() > KINEMATIC_TELEPORT_DISTANCE) {
            // リスポーン・合わせ直しなどで大きく離れた。速度で追うと周りを弾き飛ばすので瞬間移動する
            translateCharacter(*avatar.model, offset, Vector3{} - physics.getLinearVelocity(*root));
            continue;
        }
        // 動的ボディのまま速度で追従させる(接触・Touchedを保ち、押せる物体は押す)
        physics.setLinearVelocity(*root, offset * (1.0f / dt));
        physics.setAngularVelocity(*root, angularVelocityToward(current.Rotation, target.Rotation, dt));
    }
}

void ReplicationManager::collectMechanism(const std::shared_ptr<Seat>& seat, std::vector<uint32_t>& netIds,
                                          std::vector<std::shared_ptr<Motor>>& motors) const {
    if (!seat || !m_workspace) return;
    // 拘束でつながる部品同士の辺(NoCollision・Gyroは物を結び付けないので除く)
    std::unordered_map<BaseCube*, std::vector<std::pair<BaseCube*, Instance*>>> edges;
    for (Instance* value : m_workspace->getRenderConstraints()) {
        if (!value || value->IsA("NoCollision") || value->IsA("Gyro")) continue;
        auto* constraint = dynamic_cast<PhysicsConstraint*>(value);
        if (!constraint || !constraint->Enabled) continue;
        const auto cube0 = constraint->getCube0();
        const auto cube1 = constraint->getCube1();
        if (!cube0 || !cube1) continue;
        edges[cube0.get()].emplace_back(cube1.get(), value);
        edges[cube1.get()].emplace_back(cube0.get(), value);
    }

    // 着席しているキャラ(Seatと溶接される)には入らない
    std::unordered_set<BaseCube*> visited{seat.get()};
    std::unordered_set<Instance*> seenMotors;
    std::vector<BaseCube*> queue{seat.get()};
    for (size_t i = 0; i < queue.size(); ++i) {
        const auto found = edges.find(queue[i]);
        if (found == edges.end()) continue;
        for (const auto& [neighbor, constraint] : found->second) {
            if (isCharacterPart(*neighbor)) continue;
            if (constraint->IsA("Motor") && seenMotors.insert(constraint).second) {
                if (auto motor = std::dynamic_pointer_cast<Motor>(constraint->shared_from_this()))
                    motors.push_back(std::move(motor));
            }
            if (visited.insert(neighbor).second) queue.push_back(neighbor);
        }
    }
    for (const auto& [netId, obj] : m_hostObjects) {
        const auto cube = obj.cube.lock();
        if (cube && visited.contains(cube.get())) netIds.push_back(netId);
    }
}

void ReplicationManager::hostUpdateOwnership() {
    for (auto& [id, avatar] : m_remoteAvatars) {
        const auto seat = avatar.humanoid ? avatar.humanoid->getSeat() : nullptr;
        const auto ownedSeat = avatar.ownedSeat.lock();
        const PeerId peer = id;
        const bool ownsObjects = std::any_of(m_ownership.begin(), m_ownership.end(),
            [peer](const auto& entry) { return entry.second == peer; });
        if (seat && seat == ownedSeat) continue;
        if (!seat && !ownsObjects) continue;
        if (ownsObjects) hostReclaimOwnership(id);
        avatar.ownedSeat = seat;
        if (seat) hostAssignOwnership(id, seat);
    }
}

void ReplicationManager::hostAssignOwnership(PeerId id, const std::shared_ptr<Seat>& seat) {
    std::vector<uint32_t> netIds;
    std::vector<std::shared_ptr<Motor>> motors;
    collectMechanism(seat, netIds, motors);
    size_t assigned = 0;
    for (uint32_t netId : netIds) {
        HostSyncedObject& obj = m_hostObjects[netId];
        if (obj.owner != 0 && obj.owner != id) continue; // 他のClientが運転中の物体は渡さない
        const auto cube = obj.cube.lock();
        if (!cube) continue;
        obj.owner = id;
        obj.snapshots.clear();
        obj.lastLinearVelocity = m_physics && m_physics->hasBody(*cube) ? m_physics->getLinearVelocity(*cube) : Vector3{};
        obj.lastAngularVelocity = m_physics && m_physics->hasBody(*cube) ? m_physics->getAngularVelocity(*cube) : Vector3{};
        // 所有Clientの演算結果へキネマティックで追従させる
        cube->setAnchored(true);
        m_ownership[netId] = id;
        ++assigned;
    }
    if (assigned == 0) return;
    for (auto& motor : motors) {
        OwnedMotor entry;
        entry.motor = motor;
        entry.owner = id;
        m_ownedMotors.push_back(entry);
    }
    m_ownershipDirty = true;
    RCBN_LOG("Replication: gave " << assigned << " object(s) and " << motors.size()
             << " motor(s) of " << seat->getWorkspaceRelativePath() << " to peer " << id);
}

void ReplicationManager::hostReclaimOwnership(PeerId id) {
    std::vector<HostSyncedObject*> reclaimed;
    for (auto& [netId, obj] : m_hostObjects) {
        (void)netId;
        if (obj.owner != id) continue;
        obj.owner = 0;
        obj.snapshots.clear();
        if (const auto cube = obj.cube.lock()) {
            cube->setAnchored(false);
            reclaimed.push_back(&obj);
        }
    }
    std::erase_if(m_ownership, [id](const auto& entry) { return entry.second == id; });
    std::erase_if(m_ownedMotors, [id](const OwnedMotor& motor) { return motor.owner == id; });
    if (reclaimed.empty()) return;
    // 全部品を動的に戻してから、所有Clientの最後の速度を引き継ぐ(同じnative bodyは1回だけ)
    if (m_physics) {
        std::unordered_set<std::uint64_t> restored;
        for (HostSyncedObject* obj : reclaimed) {
            const auto cube = obj->cube.lock();
            if (!cube || !m_physics->hasBody(*cube)) continue;
            const PhysicsBodyHandle handle = m_physics->getBodyHandle(*cube);
            if (!handle || !restored.insert(handle.value).second) continue;
            m_physics->setLinearVelocity(*cube, obj->lastLinearVelocity);
            m_physics->setAngularVelocity(*cube, obj->lastAngularVelocity);
        }
    }
    m_ownershipDirty = true;
    RCBN_LOG("Replication: reclaimed " << reclaimed.size() << " object(s) from peer " << id);
}

void ReplicationManager::hostSendOwnership() {
    auto& net = NetworkManager::get();
    const size_t rosterSize = net.getRoster().size();
    if (rosterSize > m_ownershipRosterSize) m_ownershipDirty = true;
    m_ownershipRosterSize = rosterSize;
    if (!m_ownershipDirty) return;

    const size_t count = (std::min)(m_ownership.size(), static_cast<size_t>(UINT16_MAX));
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::Ownership));
    w.writeU16(static_cast<uint16_t>(count));
    size_t written = 0;
    for (const auto& [netId, owner] : m_ownership) {
        if (written++ >= count) break;
        w.writeU32(netId);
        w.writeU32(owner);
    }
    // 送れなかった(まだ相手がいない)ときは、次のフレームで送り直す
    if (net.sendBytes(w.data, NetworkChannel::Reliable)) m_ownershipDirty = false;
}

void ReplicationManager::hostSendJointDrives() {
    std::erase_if(m_ownedMotors, [](const OwnedMotor& entry) { return entry.motor.expired(); });
    std::vector<OwnedMotor*> changed;
    for (auto& entry : m_ownedMotors) {
        const auto motor = entry.motor.lock();
        if (entry.hasSent && entry.driveVelocity == motor->getDriveVelocity() &&
            entry.maxForce == motor->getMaxForce() && entry.servo == motor->getServo() &&
            entry.targetAngle == motor->getTargetAngle())
            continue;
        changed.push_back(&entry);
    }
    if (changed.empty()) return;

    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::JointDrive));
    w.writeU16(static_cast<uint16_t>((std::min)(changed.size(), static_cast<size_t>(UINT16_MAX))));
    for (size_t i = 0; i < changed.size() && i < UINT16_MAX; ++i) {
        const auto motor = changed[i]->motor.lock();
        const std::string path = motor->getWorkspaceRelativePath();
        const size_t length = (std::min)(path.size(), static_cast<size_t>(UINT16_MAX));
        w.writeU16(static_cast<uint16_t>(length));
        for (size_t c = 0; c < length; ++c) w.writeU8(static_cast<uint8_t>(path[c]));
        w.writeF32(motor->getDriveVelocity());
        w.writeF32(motor->getMaxForce());
        w.writeU8(motor->getServo() ? 1 : 0);
        w.writeF32(motor->getTargetAngle());
    }
    if (!NetworkManager::get().sendBytes(w.data, NetworkChannel::Reliable)) return;
    for (OwnedMotor* entry : changed) {
        const auto motor = entry->motor.lock();
        entry->hasSent = true;
        entry->driveVelocity = motor->getDriveVelocity();
        entry->maxForce = motor->getMaxForce();
        entry->servo = motor->getServo();
        entry->targetAngle = motor->getTargetAngle();
    }
}

void ReplicationManager::hostReceiveOwnedTransforms(PeerId id, const uint8_t* payload, size_t len) {
    ByteReader r{payload, len};
    uint32_t tick = 0;
    uint16_t count = 0;
    if (!r.readU32(tick) || !r.readU16(count)) return;
    m_peerClocks[id].noteTick(tick);
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t netId = 0;
        Vector3 pos;
        Quaternion rot;
        Vector3 linearVelocity;
        Vector3 angularVelocity;
        if (!r.readU32(netId) || !r.readVector3(pos) || !r.readQuat(rot) ||
            !r.readVector3(linearVelocity) || !r.readVector3(angularVelocity)) return;
        if (!isFiniteWireVector(pos) || !isFiniteWireQuat(rot) ||
            !isFiniteWireVector(linearVelocity) || !isFiniteWireVector(angularVelocity)) {
            RCBN_WARN("Replication: OwnedTransforms entry with non-finite values ignored (peer=" << id << ")");
            continue;
        }
        auto it = m_hostObjects.find(netId);
        // 主導権を渡していない物体の姿勢は受け付けない
        if (it == m_hostObjects.end() || it->second.owner != id) continue;
        pushSnapshot(it->second.snapshots, tick, CFrame(pos, rot));
        it->second.lastLinearVelocity = linearVelocity;
        it->second.lastAngularVelocity = angularVelocity;
    }
}

void ReplicationManager::hostStepOwnedObjects(Physics& physics) {
    std::unordered_set<std::uint64_t> drivenBodies;
    for (auto& [netId, obj] : m_hostObjects) {
        (void)netId;
        if (obj.owner == 0 || obj.snapshots.empty()) continue;
        const auto clock = m_peerClocks.find(obj.owner);
        if (clock == m_peerClocks.end() || !clock->second.hasStepTick()) continue;
        const auto cube = obj.cube.lock();
        if (!cube || !cube->Anchored || !physics.hasBody(*cube)) continue;
        CFrame pose;
        sampleSnapshots(obj.snapshots, clock->second.stepTick(), pose);
        setSyncedWorldCFrame(*cube, pose);
        driveKinematicBody(physics, *cube, pose, drivenBodies);
    }
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

    std::unordered_set<BaseCube*> ownedCubes;
    for (const auto& [netId, obj] : m_hostObjects) {
        (void)netId;
        if (obj.owner == 0) continue;
        if (const auto cube = obj.cube.lock()) ownedCubes.insert(cube.get());
    }

    std::vector<std::shared_ptr<BaseCube>> targets;
    for (auto& [name, child] : m_workspace->getChildren()) {
        collectSyncTargets(child, excludedCharacters, ownedCubes, targets);
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
    m_hostClock.noteTick(hostTick);

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
    (void)dt;
    // 物理ボディを持たない同期物体だけ、ここでフレームごとに表示姿勢を与える
    // (ボディを持つものはclientStepWorldObjectsが固定stepごとに動かす)
    for (auto& [id, obj] : m_clientObjects) {
        if (!obj.hasTarget || obj.owned) continue;
        auto cube = obj.cube.lock();
        if (!cube) continue;
        if (m_physics && m_physics->hasBody(*cube)) continue;

        // Host tick基準で「少し前」の姿勢を前後の受信姿勢から補間する
        CFrame displayed = obj.target;
        if (m_hostClock.hasRenderTick()) sampleSnapshots(obj.snapshots, m_hostClock.renderTick(), displayed);
        setSyncedWorldCFrame(*cube, displayed);
    }
}

void ReplicationManager::ensurePreStepCallback() {
    if (!m_physics || m_preStepCallbackPhysics == m_physics) return;
    // 以前のPhysicsへの登録は残るが、コールバック側で現在のPhysics以外を無視する
    m_physics->addPreStepCallback(m_lifetimeToken, [this](Physics& physics, float dt) {
        onPreStep(physics, dt);
    });
    m_preStepCallbackPhysics = m_physics;
}

void ReplicationManager::onPreStep(Physics& physics, float dt) {
    if (&physics != m_physics) return;
    // Client: Hostが演算する同期物体 / Host: 各Clientが演算するキャラ・乗り物への追従。
    // 対象が無い側は何もしないので、役割で分けずに両方呼ぶ
    clientStepWorldObjects(physics);
    for (auto& [id, clock] : m_peerClocks) {
        (void)id;
        if (clock.hasStepTick()) clock.beginStep();
    }
    hostStepProxies(physics, dt);
    hostStepOwnedObjects(physics);
}

void ReplicationManager::driveKinematicBody(Physics& physics, BaseCube& cube, const CFrame& pose,
                                            std::unordered_set<std::uint64_t>& drivenBodies) {
    // Weldで1つのnative bodyにまとまった同期物体(車体+Seatなど)は、memberの論理姿勢を
    // 書き換えても物理同期でbody poseに戻されるため、native bodyごとに1回だけ移動目標を渡す。
    // 目標はこのstepで到達する速度として設定され、次のstepで次の目標に置き換わる
    const PhysicsBodyHandle handle = physics.getBodyHandle(cube);
    if (!handle || !drivenBodies.insert(handle.value).second) return;
    // 表示時刻の合わせ直し・初回などで目標が大きく離れているときは、速度で動かすと
    // 1stepで高速に突き抜けて周りを弾き飛ばすため、瞬間移動させる
    const Vector3 jump = pose.Position - physics.getMemberWorldCFrame(cube).Position;
    if (jump.length() > KINEMATIC_TELEPORT_DISTANCE)
        physics.setMemberWorldCFrame(cube, pose);
    else
        physics.setMemberKinematicTarget(cube, pose);
}

void ReplicationManager::clientStepWorldObjects(Physics& physics) {
    if (&physics != m_physics || m_clientObjects.empty() || !m_hostClock.hasStepTick()) return;
    // これからシミュレートするstepの後の時刻
    const double stepTick = m_hostClock.beginStep();

    std::unordered_set<std::uint64_t> drivenBodies;
    for (auto& [id, obj] : m_clientObjects) {
        (void)id;
        if (!obj.hasTarget || obj.owned) continue;
        auto cube = obj.cube.lock();
        if (!cube || !cube->Anchored || !physics.hasBody(*cube)) continue;

        CFrame pose = obj.target;
        sampleSnapshots(obj.snapshots, stepTick, pose);
        setSyncedWorldCFrame(*cube, pose);
        driveKinematicBody(physics, *cube, pose, drivenBodies);
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

double ReplicationManager::interpolationDelayTicks() const {
    return static_cast<double>(interpolationDelaySeconds()) * 60.0;
}

void ReplicationManager::clientAdvanceRenderClock(float dt) {
    m_hostClock.advance(dt, interpolationDelayTicks());
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
        despawnAllAvatars(); // 主導権を渡していた物体もHostへ戻す
        m_latestPoses.clear();
        m_latestVels.clear();
        m_peerClocks.clear();
        clientReleaseWorldObjects();
        m_pathToNetId.clear();
        m_hostObjects.clear();
        m_ownership.clear();
        m_ownedMotors.clear();
        m_ownershipDirty = false;
        m_ownershipRosterSize = 0;
        m_worldMappingDirty = false;
        m_prevRosterSize = 0;
        m_hostClock.reset();
        m_localWasSeated = false;
        m_sentSeatPaths.clear();
        m_seatStateRosterSize = 0;
    } else if (oldRole == NetworkRole::Client && newRole == NetworkRole::Host) {
        m_hostClock.reset();
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

void ReplicationManager::clientSendAvatarState() {
    auto& net = NetworkManager::get();
    if (!net.hasPeers() || !m_physics || !m_user || !m_user->humanoid) return;
    const Humanoid& humanoid = *m_user->humanoid;
    const auto root = humanoid.getRootPart();
    if (!root) return;
    // 着席中の操作軸はローカルのSeatに書かれている(Host上のSeatへ写す)
    float forwardAxis = 0.0f;
    float rightAxis = 0.0f;
    if (const auto seat = humanoid.getSeat()) {
        forwardAxis = seat->Throttle;
        rightAxis = seat->Steer;
    }
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(MessageType::AvatarState));
    w.writeU32(static_cast<uint32_t>(m_physics->getSimulationTick() & 0xffffffffu));
    const CFrame pose = root->getWorldCFrame();
    w.writeVector3(pose.Position);
    w.writeQuat(pose.Rotation);
    w.writeVector3(m_physics->hasBody(*root) ? m_physics->getLinearVelocity(*root) : Vector3{});
    w.writeF32(humanoid.getWalkCycle());
    w.writeU8(humanoid.getIsGrounded() ? 1 : 0);
    w.writeF32(forwardAxis);
    w.writeF32(rightAxis);
    net.sendBytes(w.data, NetworkChannel::Unreliable);
}

void ReplicationManager::clientSendOwnedTransforms() {
    auto& net = NetworkManager::get();
    if (!net.hasPeers() || !m_physics) return;
    struct Entry {
        uint32_t netId;
        CFrame pose;
        Vector3 linearVelocity;
        Vector3 angularVelocity;
    };
    std::vector<Entry> entries;
    for (const auto& [netId, obj] : m_clientObjects) {
        if (!obj.owned) continue;
        const auto cube = obj.cube.lock();
        if (!cube || !m_physics->hasBody(*cube)) continue;
        entries.push_back({netId, cube->getWorldCFrame(),
                           m_physics->getLinearVelocity(*cube), m_physics->getAngularVelocity(*cube)});
    }
    if (entries.empty()) return;

    const uint32_t tick = static_cast<uint32_t>(m_physics->getSimulationTick() & 0xffffffffu);
    for (size_t offset = 0; offset < entries.size(); offset += TRANSFORMS_PER_PACKET) {
        const size_t count = (std::min)(TRANSFORMS_PER_PACKET, entries.size() - offset);
        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::OwnedTransforms));
        w.writeU32(tick);
        w.writeU16(static_cast<uint16_t>(count));
        for (size_t i = 0; i < count; ++i) {
            const Entry& entry = entries[offset + i];
            w.writeU32(entry.netId);
            w.writeVector3(entry.pose.Position);
            w.writeQuat(entry.pose.Rotation);
            w.writeVector3(entry.linearVelocity);
            w.writeVector3(entry.angularVelocity);
        }
        net.sendBytes(w.data, NetworkChannel::Unreliable);
    }
}

void ReplicationManager::clientNotifyStandUp() {
    const bool seated = m_user && m_user->humanoid && m_user->humanoid->isSeated();
    if (m_localWasSeated && !seated) {
        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::StandUpRequest));
        NetworkManager::get().sendBytes(w.data, NetworkChannel::Reliable);
    }
    m_localWasSeated = seated;
}

void ReplicationManager::clientApplyOwnership(const uint8_t* payload, size_t len) {
    ByteReader r{payload, len};
    uint16_t count = 0;
    if (!r.readU16(count)) return;
    const PeerId localId = NetworkManager::get().getLocalPeerId();
    std::unordered_set<uint32_t> mine;
    for (uint16_t i = 0; i < count; ++i) {
        uint32_t netId = 0;
        uint32_t owner = 0;
        if (!r.readU32(netId) || !r.readU32(owner)) return;
        if (owner == localId) mine.insert(netId);
    }
    size_t acquired = 0;
    size_t released = 0;
    for (auto& [netId, obj] : m_clientObjects) {
        const bool owned = mine.contains(netId);
        if (owned == obj.owned) continue;
        const auto cube = obj.cube.lock();
        if (!cube) continue;
        obj.owned = owned;
        // 自分が演算する物体は動的に戻し、Hostへ返した物体は再びキネマティックで追従させる
        cube->setAnchored(!owned);
        if (owned) ++acquired;
        else ++released;
    }
    if (acquired > 0 || released > 0)
        RCBN_LOG("Replication: ownership acquired " << acquired << ", released " << released << " object(s)");
}

void ReplicationManager::clientApplyJointDrive(const uint8_t* payload, size_t len) {
    ByteReader r{payload, len};
    uint16_t count = 0;
    if (!r.readU16(count) || !m_workspace) return;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t pathLength = 0;
        if (!r.readU16(pathLength) || r.remaining < pathLength) return;
        std::string path(reinterpret_cast<const char*>(r.p), pathLength);
        r.p += pathLength;
        r.remaining -= pathLength;
        float driveVelocity = 0.0f;
        float maxForce = 0.0f;
        uint8_t servo = 0;
        float targetAngle = 0.0f;
        if (!r.readF32(driveVelocity) || !r.readF32(maxForce) || !r.readU8(servo) || !r.readF32(targetAngle)) return;
        if (!std::isfinite(driveVelocity) || !std::isfinite(maxForce) || !std::isfinite(targetAngle)) continue;
        Instance* found = m_workspace->getChildByPath(path);
        auto motor = found ? std::dynamic_pointer_cast<Motor>(found->shared_from_this()) : nullptr;
        if (!motor) continue;
        // 同じ値の再設定でジョイントを作り直さないよう、変わったものだけ設定する
        if (motor->getDriveVelocity() != driveVelocity) motor->setDriveVelocity(driveVelocity);
        if (motor->getMaxForce() != maxForce) motor->setMaxForce(maxForce);
        if (motor->getServo() != (servo != 0)) motor->setServo(servo != 0);
        if (motor->getTargetAngle() != targetAngle) motor->setTargetAngle(targetAngle);
    }
}
