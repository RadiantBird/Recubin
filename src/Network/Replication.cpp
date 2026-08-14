#include <Network/Replication.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/ByteStream.hpp>
#include <Network/NetworkIdentity.hpp>

#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#include <Core/Physics.hpp>
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Instances/NoCollision.hpp>
#include <Instances/Humanoid.hpp>
#include <Instances/Cube.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

#ifdef max
#undef max
#endif

ReplicationManager::ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot)
    : m_workspace(workspace), m_user(user), m_characterSearchRoot(characterSearchRoot),
      m_physics(workspace ? workspace->getPhysicsEngine() : nullptr) {
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
    m_pendingAvatarInput.clear();
    m_lastProcessedSeq.clear();
    m_pathToNetId.clear();
    m_hostObjects.clear();
    m_clientObjects.clear();

    if (m_predictionPhysics) m_predictionPhysics->clearCubes();
    m_predictionStaticMirror.clear();
    m_predictionHumanoid.reset();
    m_shadowRoot.reset();
    m_predictionPhysics.reset();
    m_predictionSceneReady = false;
    m_predictionRescanTimer = 0.0f;

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
    applyAvatarPoses(dt);

    if (net.getRole() == NetworkRole::Host) {
        hostSendSimulationClock(dt, m_physics);
        hostUpdateWorld(dt);
    } else if (net.getRole() == NetworkRole::Client) {
        ensurePredictionScene();
        if (m_predictionSceneReady) {
            m_predictionRescanTimer += dt;
            if (m_predictionRescanTimer >= 2.0f) {
                m_predictionRescanTimer = 0.0f;
                rescanPredictionStaticGeometry();
            }
            syncPredictionShadowToLocal();
        }
        bufferLocalInput(dt);
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

    CFrame local;
    if (!getLocalRootCFrame(local)) return;

    if (net.getRole() == NetworkRole::Client) {
        if (!net.hasPeers() || !m_user || m_inputHistory.empty()) return;
        const auto& entry = m_inputHistory.back();
        const auto& in = entry.input;
        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::AvatarState));
        w.writeVector3(in.flatForward);
        w.writeVector3(in.flatRight);
        w.writeVector3(in.targetMoveDir);
        bool standUpPending = m_standUpRequestSeq != 0 && m_standUpRequestSeq > m_hostAckedSeq;
        uint8_t flags = (in.isPressingMove ? 1 : 0) | (in.ctrlLockEnabled ? 2 : 0) |
                        (m_pendingJumpLatch ? 4 : 0) | (standUpPending ? 8 : 0);
        w.writeU8(flags);
        w.writeF32(in.forwardAxis);
        w.writeF32(in.rightAxis);
        w.writeU32(in.seq);
        net.sendBytes(w.data, NetworkChannel::Unreliable);
        m_pendingJumpLatch = false;
    } else if (net.getRole() == NetworkRole::Host) {
        m_latestPoses[net.getLocalPeerId()] = local;

        // 自分(ホスト)の速度を記録してからAvatarBatchに載せる
        Vector3 localVel{};
        auto localRoot = (m_user && m_user->humanoid) ? m_user->humanoid->getRootPart() : nullptr;
        Physics* physics = m_workspace ? m_workspace->getPhysicsEngine() : nullptr;
        if (localRoot && physics) localVel = physics->getLinearVelocity(*localRoot);
        m_latestVels[net.getLocalPeerId()] = localVel;

        std::unordered_set<PeerId> rosterIds;
        for (const auto& info : net.getRoster()) rosterIds.insert(info.id);
        rosterIds.insert(net.getLocalPeerId());

        std::vector<std::pair<PeerId, CFrame>> entries;
        for (const auto& [id, pose] : m_latestPoses) {
            if (rosterIds.find(id) == rosterIds.end()) continue;
            entries.emplace_back(id, pose);
            if (entries.size() >= 255) break;
        }
        if (entries.empty()) return;

        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::AvatarBatch));
        w.writeU8(static_cast<uint8_t>(entries.size()));
        for (const auto& [id, pose] : entries) {
            auto velIt = m_latestVels.find(id);
            Vector3 vel = velIt != m_latestVels.end() ? velIt->second : Vector3{};
            float walkCycle = 0.0f;
            bool grounded = true;
            bool seated = false;
            if (id == net.getLocalPeerId()) {
                if (m_user && m_user->humanoid) {
                    walkCycle = m_user->humanoid->getWalkCycle();
                    grounded = m_user->humanoid->getIsGrounded();
                    seated = m_user->humanoid->isSeated();
                }
            } else if (auto avatarIt = m_remoteAvatars.find(id); avatarIt != m_remoteAvatars.end() && avatarIt->second.humanoid) {
                walkCycle = avatarIt->second.humanoid->getWalkCycle();
                grounded = avatarIt->second.humanoid->getIsGrounded();
                seated = avatarIt->second.humanoid->isSeated();
            }
            w.writeU32(id);
            w.writeVector3(pose.Position);
            w.writeQuat(pose.Rotation);
            w.writeVector3(vel);
            auto seqIt = m_lastProcessedSeq.find(id);
            w.writeU32(seqIt != m_lastProcessedSeq.end() ? seqIt->second : 0u);
            w.writeF32(walkCycle);
            w.writeU8(static_cast<uint8_t>((grounded ? 1 : 0) | (seated ? 2 : 0)));
        }
        net.sendBytes(w.data, NetworkChannel::Unreliable);
    }
}

void ReplicationManager::onGameMessage(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId) {
    const std::uint64_t receivedGeneration = m_workspaceGeneration;
    const auto receivedWorkspace = m_workspace;
    auto bindingIsCurrent = [&]() {
        return receivedGeneration == m_workspaceGeneration &&
               receivedWorkspace == m_workspace;
    };
    if (type == static_cast<uint8_t>(MessageType::AvatarState)) {
        ByteReader r{payload, len};
        AvatarInputWire in;
        uint8_t flags = 0;
        if (!r.readVector3(in.flatForward) || !r.readVector3(in.flatRight) || !r.readVector3(in.targetMoveDir)
            || !r.readU8(flags) || !r.readF32(in.forwardAxis) || !r.readF32(in.rightAxis) || !r.readU32(in.seq)) return;
        auto finiteVector = [](const Vector3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if (!finiteVector(in.flatForward) || !finiteVector(in.flatRight) || !finiteVector(in.targetMoveDir)
            || !std::isfinite(in.forwardAxis) || !std::isfinite(in.rightAxis)) return;
        uint32_t newestSeq = 0;
        if (auto it = m_lastProcessedSeq.find(senderId); it != m_lastProcessedSeq.end()) newestSeq = it->second;
        if (auto it = m_pendingAvatarInput.find(senderId); it != m_pendingAvatarInput.end()) {
            if (it->second.seq > newestSeq) newestSeq = it->second.seq;
        }
        if (in.seq <= newestSeq) return;
        in.isPressingMove  = (flags & 1) != 0;
        in.ctrlLockEnabled = (flags & 2) != 0;
        in.jumpRequested   = (flags & 4) != 0;
        in.standUpRequested = (flags & 8) != 0;
        auto flattenAndClamp = [](Vector3 v) {
            v.y = 0.0f;
            float len = v.length();
            return (len > 1.0f) ? (v * (1.0f / len)) : v;
        };
        in.flatForward = flattenAndClamp(in.flatForward);
        in.flatRight = flattenAndClamp(in.flatRight);
        in.targetMoveDir = in.isPressingMove ? flattenAndClamp(in.targetMoveDir) : Vector3{};
        in.forwardAxis = std::clamp(in.forwardAxis, -1.0f, 1.0f);
        in.rightAxis = std::clamp(in.rightAxis, -1.0f, 1.0f);
        m_pendingAvatarInput[senderId] = in;
    } else if (type == static_cast<uint8_t>(MessageType::AvatarBatch)) {
        ByteReader r{payload, len};
        uint8_t count = 0;
        if (!r.readU8(count)) return;

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
            if (id == localId) {
                m_hostAuthoritativeSelfPose = CFrame(pos, rot);
                m_hasHostAuthoritativeSelfPose = true;
                m_hostAckedSeq = lastProcessedSeq;
                m_hostAuthoritativeSelfVel = vel;
                continue;
            }
            m_latestPoses[id] = CFrame(pos, rot);
            if (auto avatarIt = m_remoteAvatars.find(id); avatarIt != m_remoteAvatars.end()) {
                avatarIt->second.walkCycle = walkCycle;
                avatarIt->second.grounded = (visualFlags & 1) != 0;
                avatarIt->second.seated = (visualFlags & 2) != 0;
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
        if (info.id == localId) continue;
        if (m_remoteAvatars.find(info.id) == m_remoteAvatars.end()) {
            spawnRemoteAvatar(info.id);
        }
    }

    std::unordered_set<PeerId> rosterIds;
    for (const auto& info : net.getRoster()) rosterIds.insert(info.id);

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

    // 既に物理参加中の全Root(ローカルUserのRoot + 他の物理プロキシ)との衝突を除外する
    // (プレイヤー同士のPvP衝突は今回スコープ外のため)
    std::vector<std::shared_ptr<BaseCube>> otherRoots;
    if (m_user && m_user->character) {
        for (auto& [name, child] : m_user->character->getChildren()) {
            if (name != "Root") continue;
            if (auto localRoot = std::dynamic_pointer_cast<BaseCube>(child)) otherRoots.push_back(localRoot);
        }
    }
    for (auto& [otherId, otherAvatar] : m_remoteAvatars) {
        if (otherId == id) continue;
        if (otherAvatar.isPhysicsProxy && otherAvatar.humanoid) {
            if (auto otherRoot = otherAvatar.humanoid->getRootPart())
                otherRoots.push_back(otherRoot);
        }
    }
    for (auto& otherRoot : otherRoots) {
        auto nc = std::make_shared<NoCollision>(root, otherRoot);
        avatar.model->addChild(nc);
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
        if (humanoid) humanoid->resolveParts(model.get());
    }

    std::vector<BaseCube*> cubes;
    for (auto& [name, child] : model->getChildren()) {
        collectBaseCubes(child, cubes);
    }
    for (BaseCube* cube : cubes) {
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
    Quaternion invR = root->cframe.Rotation.conjugate();
    for (BaseCube* cube : cubes) {
        CFrame rel;
        rel.Position = invR.rotate(cube->cframe.Position - root->cframe.Position);
        rel.Rotation = invR * cube->cframe.Rotation;
        avatar.parts.emplace_back(cube, rel);
    }

    auto identity = User::createRemoteUser(id);
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
    m_latestPoses.erase(id);
    m_latestVels.erase(id);
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
        auto it = m_latestPoses.find(id);
        if (it == m_latestPoses.end()) continue;
        const CFrame& pose = it->second;

        if (!avatar.hasPose) {
            avatar.current = pose;
            avatar.hasPose = true;
            RCBN_LOG("Replication: first pose for peer " << id);
        } else {
            float alpha = 1.0f - std::exp(-15.0f * dt);
            avatar.current.Position = avatar.current.Position + (pose.Position - avatar.current.Position) * alpha;
            avatar.current.Rotation = Quaternion::Slerp(avatar.current.Rotation, pose.Rotation, alpha);
        }

        for (auto& [part, rel] : avatar.parts) {
            part->cframe = avatar.current * rel;
        }
        if (avatar.humanoid) {
            avatar.humanoid->setWalkCycle(avatar.walkCycle);
            avatar.humanoid->setIsGroundedForReplication(avatar.grounded);
            avatar.humanoid->setSeatedForReplication(avatar.seated);
            avatar.humanoid->applyBodyAnimation(false, false);
        }
    }
}

void ReplicationManager::reconcileLocalPose() {
    if (!m_hasHostAuthoritativeSelfPose) return;
    m_hasHostAuthoritativeSelfPose = false; // 1回消費
    uint32_t ackSeq = m_hostAckedSeq;

    // ack済み(Hostが処理済み)の入力履歴は不要なので破棄する
    while (!m_inputHistory.empty() && m_inputHistory.front().seq <= ackSeq) {
        m_inputHistory.pop_front();
    }

    if (!m_user || !m_user->humanoid) return;
    auto root = m_user->humanoid->getRootPart();
    if (!root) return;

    CFrame local;
    if (!getLocalRootCFrame(local)) return;

    Vector3 diff = m_hostAuthoritativeSelfPose.Position - local.Position;
    float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    float preReplayDrift = std::sqrt(distSq);
    auto finiteVector = [](const Vector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finiteVector(local.Position) || !finiteVector(m_hostAuthoritativeSelfPose.Position)) return;

    // localhostの実シーンで静止中も3.5〜3.9studの物理差が観測される。
    // この誤差はClientのローカル表示にしか影響せず、Host/他ピアはHost物理姿勢を使うため受容できる。
    constexpr float kAcceptedError = 4.5f;
    constexpr float kHardSnapError = 12.0f;

    auto applyCorrectedPose = [&](const CFrame& target, bool hardSnap, float error) {
        CFrame corrected = target;
        if (!hardSnap) {
            Vector3 delta = target.Position - local.Position;
            float alpha = 0.5f / error;
            if (alpha > 0.25f) alpha = 0.25f; // 1回の可視補正は最大0.5stud
            corrected.Position = local.Position + delta * alpha;
            corrected.Rotation = Quaternion::Slerp(local.Rotation, target.Rotation, 0.25f);
        }
        Physics* physics = m_workspace ? m_workspace->getPhysicsEngine() : nullptr;
        if (physics) physics->setBodyWorldCFrame(*root, corrected);
        root->cframe = corrected;
    };

    // 予測シーンがまだ使えない初期フレームだけは、Host姿勢を直接目標にする。
    if (!m_predictionSceneReady || !m_shadowRoot || !m_predictionHumanoid || m_inputHistory.empty()) {
        if (preReplayDrift <= kAcceptedError) return;
        bool hardSnap = preReplayDrift > kHardSnapError || !std::isfinite(preReplayDrift);
        applyCorrectedPose(m_hostAuthoritativeSelfPose, hardSnap, preReplayDrift);
        RCBN_LOG("Replication: " << (hardSnap ? "hard" : "soft")
                 << " fallback correction (error=" << preReplayDrift << ")");
        return;
    }

    // ---- 入力リプレイ ----
    // 一番古い未ack入力の「適用前」状態(巻き戻し先)。以降の巻き戻し・リプレイ準備で使う
    const auto& firstEntry = m_inputHistory.front();

    // 1. シャドウRootをHost権威姿勢へ巻き戻す
    //    回転はauthoritative姿勢ではなくfirstEntry.rotationBeforeを使う
    //    (actorとcframeの回転を一致させ、後段のsyncAllCubes()による上書きを正しい値にするため)
    if (m_predictionPhysics && m_predictionPhysics->hasBody(*m_shadowRoot)) {
        CFrame shadowBodyCFrame = m_hostAuthoritativeSelfPose;
        shadowBodyCFrame.Rotation = firstEntry.rotationBefore;
        m_predictionPhysics->setBodyWorldCFrame(*m_shadowRoot, shadowBodyCFrame);
        m_predictionPhysics->setLinearVelocity(*m_shadowRoot, m_hostAuthoritativeSelfVel);
        m_predictionPhysics->setAngularVelocity(*m_shadowRoot, Vector3());
    }
    m_shadowRoot->cframe = m_hostAuthoritativeSelfPose;
    m_shadowRoot->cframe.Rotation = firstEntry.rotationBefore;

    // 2. Humanoidの内部補間状態を、一番古い未ack入力の「適用前」状態へ復元
    m_predictionHumanoid->setCurrentMoveDir(firstEntry.currentMoveDirBefore);
    m_predictionHumanoid->setWalkCycle(firstEntry.walkCycleBefore);

    // 3. 未ack入力を古い順に1件ずつ再生(1エントリ=1回のmove()+stepOnce()を厳守)
    for (const auto& entry : m_inputHistory) {
        if (entry.input.standUpRequested) m_predictionHumanoid->standUp(m_predictionPhysics.get());
        if (entry.input.jumpRequested) m_predictionHumanoid->jump(m_predictionPhysics.get());
        m_predictionHumanoid->move(entry.input.flatForward, entry.input.flatRight, entry.input.isPressingMove,
                                    entry.input.targetMoveDir, entry.input.ctrlLockEnabled, m_predictionPhysics.get(),
                                    false, false, entry.input.forwardAxis, entry.input.rightAxis, entry.dt);
        m_predictionPhysics->stepOnce(entry.dt);
        m_predictionPhysics->syncAllCubes();
    }

    // 4. リプレイ結果と「同じ時点の現在予測」の差だけを本当の誤差として評価する。
    //    Hostのack時点姿勢と現在姿勢を直接比較すると、正常な未ack入力分までズレと誤認する。
    CFrame result = m_shadowRoot->getWorldCFrame();
    if (!finiteVector(result.Position)) {
        applyCorrectedPose(m_hostAuthoritativeSelfPose, true, preReplayDrift);
        RCBN_LOG("Replication: hard correction after non-finite replay result");
        return;
    }
    Vector3 residualDelta = result.Position - local.Position;
    float residual = residualDelta.length();
    if (!std::isfinite(residual)) {
        applyCorrectedPose(m_hostAuthoritativeSelfPose, true, preReplayDrift);
        return;
    }
    if (residual <= kAcceptedError) return; // ローカル表示だけに存在する小さな誤差は受容

    bool hardSnap = residual > kHardSnapError;
    applyCorrectedPose(result, hardSnap, residual);
    RCBN_LOG("Replication: " << (hardSnap ? "hard" : "soft") << " replay correction, frames="
             << m_inputHistory.size() << " preDrift=" << preReplayDrift << " residual=" << residual);
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

        AvatarInputWire in;
        if (auto it = m_pendingAvatarInput.find(id); it != m_pendingAvatarInput.end()) {
            in = it->second;
        }

        // 妥当性チェック: 破損/悪意ある入力でも歩行速度定数を超えられないようクランプする
        auto clampUnit = [](Vector3 v) {
            float len = v.length();
            return (len > 1.0f) ? (v * (1.0f / len)) : v;
        };
        in.targetMoveDir = clampUnit(in.targetMoveDir);
        in.flatForward   = clampUnit(in.flatForward);
        in.flatRight     = clampUnit(in.flatRight);
        if (in.forwardAxis > 1.0f) in.forwardAxis = 1.0f;
        if (in.forwardAxis < -1.0f) in.forwardAxis = -1.0f;
        if (in.rightAxis > 1.0f) in.rightAxis = 1.0f;
        if (in.rightAxis < -1.0f) in.rightAxis = -1.0f;

        if (in.standUpRequested) avatar.humanoid->standUp(physics);
        if (in.jumpRequested) avatar.humanoid->jump(physics);
        avatar.humanoid->move(in.flatForward, in.flatRight, in.isPressingMove, in.targetMoveDir,
                               in.ctrlLockEnabled, physics, false, false, in.forwardAxis, in.rightAxis, dt);

        m_latestPoses[id] = root->getWorldCFrame();
        Vector3 vel = physics->getLinearVelocity(*root);
        m_latestVels[id] = vel;
        m_lastProcessedSeq[id] = in.seq;
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

        const CFrame& cf = cube->cframe;
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
    uint16_t count = 0;
    if (!r.readU16(count)) return;

    for (uint16_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        Vector3 pos;
        Quaternion rot;
        if (!r.readU32(id) || !r.readVector3(pos) || !r.readQuat(rot)) return;

        auto it = m_clientObjects.find(id);
        if (it == m_clientObjects.end()) continue; // マッピング未着のUNRELIABLE先行は正常系

        it->second.target = CFrame(pos, rot);
        it->second.hasTarget = true;
    }
}

void ReplicationManager::clientApplyWorldSmoothing(float dt) {
    for (auto& [id, obj] : m_clientObjects) {
        if (!obj.hasTarget) continue;
        auto cube = obj.cube.lock();
        if (!cube) continue;

        if (!obj.snapped) {
            obj.current = obj.target;
            obj.snapped = true;
        } else {
            float alpha = 1.0f - std::exp(-15.0f * dt);
            obj.current.Position = obj.current.Position + (obj.target.Position - obj.current.Position) * alpha;
            obj.current.Rotation = Quaternion::Slerp(obj.current.Rotation, obj.target.Rotation, alpha);
        }

        cube->cframe = obj.current;
    }
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
        m_predictionSceneReady = false;
        m_predictionStaticMirror.clear();
        m_predictionRescanTimer = 0.0f;
        m_standUpRequestSeq = 0;
        m_pendingJumpLatch = false;
    } else if (oldRole == NetworkRole::Client && newRole == NetworkRole::Host) {
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

void ReplicationManager::bufferLocalInput(float dt) {
    if (!m_user || !m_user->humanoid) return;
    auto root = m_user->humanoid->getRootPart();

    BufferedInput entry;
    entry.seq = m_nextSeq++;
    entry.input.flatForward     = m_user->lastMovementInput.flatForward;
    entry.input.flatRight       = m_user->lastMovementInput.flatRight;
    entry.input.targetMoveDir   = m_user->lastMovementInput.targetMoveDir;
    entry.input.isPressingMove  = m_user->lastMovementInput.isPressingMove;
    entry.input.ctrlLockEnabled = m_user->lastMovementInput.ctrlLockEnabled;
    entry.input.forwardAxis     = m_user->lastMovementInput.forwardAxis;
    entry.input.rightAxis       = m_user->lastMovementInput.rightAxis;
    entry.input.jumpRequested   = m_user->lastMovementInput.jumpRequested;
    entry.input.standUpRequested = m_user->lastMovementInput.standUpRequested;
    if (entry.input.jumpRequested) m_pendingJumpLatch = true;
    entry.input.seq             = entry.seq;
    if (entry.input.standUpRequested) m_standUpRequestSeq = entry.seq;
    entry.dt = dt;
    entry.currentMoveDirBefore = m_user->humanoid->getCurrentMoveDir();
    entry.walkCycleBefore      = m_user->humanoid->getWalkCycle();
    entry.rotationBefore       = root ? root->Rotation : Quaternion();

    m_inputHistory.push_back(entry);

    // 直近2秒程度(60fps換算で最大240件)を超えたら古いものから破棄する安全弁
    // (ackが来ずに際限なく溜まり続けることを防ぐ。通常は受信のたびにack済み分が破棄される)
    while (m_inputHistory.size() > 240) {
        m_inputHistory.pop_front();
    }
}

// ---- クライアント予測用の分離シーン(Step2) ----

void ReplicationManager::ensurePredictionScene() {
    if (m_predictionSceneReady) return;
    if (!m_user || !m_user->humanoid) return;
    auto realRoot = m_user->humanoid->getRootPart();
    if (!realRoot) return;

    m_predictionPhysics = std::make_unique<Physics>();
    m_predictionPhysics->init();
    if (m_workspace) m_predictionPhysics->setGravity(m_workspace->Gravity);

    m_shadowRoot = std::make_shared<Cube>(realRoot->getWorldPosition(), realRoot->Size, 0);
    m_shadowRoot->Name = "PredictionShadowRoot";
    m_shadowRoot->Anchored = false;
    m_shadowRoot->CanCollide = true;
    m_shadowRoot->cframe = realRoot->getWorldCFrame();
    m_predictionHumanoid = std::make_shared<Humanoid>();
    m_predictionHumanoid->setRootPart(m_shadowRoot);
    m_predictionPhysics->createActor(m_shadowRoot);

    m_predictionSceneReady = true;
    RCBN_LOG("Replication: prediction scene created (shadow root size=" << realRoot->Size.x << "," << realRoot->Size.y << "," << realRoot->Size.z << ")");
}

void ReplicationManager::syncPredictionShadowToLocal() {
    if (!m_predictionSceneReady || !m_shadowRoot || !m_predictionPhysics ||
        !m_predictionPhysics->hasBody(*m_shadowRoot)) return;
    if (!m_user || !m_user->humanoid) return;
    auto realRoot = m_user->humanoid->getRootPart();
    if (!realRoot) return;

    CFrame realCFrame = realRoot->getWorldCFrame();

    m_predictionPhysics->setBodyWorldCFrame(*m_shadowRoot, realCFrame);

    m_shadowRoot->cframe = realCFrame;

    m_predictionPhysics->setLinearVelocity(*m_shadowRoot, Vector3());
    m_predictionPhysics->setAngularVelocity(*m_shadowRoot, Vector3());
}

static void collectPredictionStaticGeometry(const std::shared_ptr<Instance>& node,
                                             std::vector<std::shared_ptr<BaseCube>>& out) {
    auto cube = std::dynamic_pointer_cast<BaseCube>(node);
    if (cube && cube->Anchored && cube->CanCollide) out.push_back(cube);
    for (auto& [name, child] : node->getChildren()) {
        collectPredictionStaticGeometry(child, out);
    }
}

void ReplicationManager::rescanPredictionStaticGeometry() {
    if (!m_predictionSceneReady || !m_workspace) return;

    std::vector<std::shared_ptr<BaseCube>> targets;
    for (auto& [name, child] : m_workspace->getChildren()) {
        collectPredictionStaticGeometry(child, targets);
    }

    std::unordered_map<std::string, std::shared_ptr<BaseCube>> newMirror;
    int added = 0;
    for (auto& target : targets) {
        std::string path = target->getWorkspaceRelativePath();
        auto existing = m_predictionStaticMirror.find(path);
        if (existing != m_predictionStaticMirror.end()) {
            newMirror[path] = existing->second;
            continue;
        }
        auto mirror = std::make_shared<Cube>(target->getWorldPosition(), target->Size, 0);
        mirror->Name = "Mirror_" + target->Name;
        mirror->Anchored = true;
        mirror->CanCollide = true;
        mirror->cframe = target->getWorldCFrame();
        m_predictionPhysics->createActor(mirror);
        newMirror[path] = mirror;
        added++;
    }

    int removed = 0;
    for (auto& [path, mirror] : m_predictionStaticMirror) {
        if (newMirror.find(path) == newMirror.end()) {
            m_predictionPhysics->removeCube(mirror);
            removed++;
        }
    }

    m_predictionStaticMirror = std::move(newMirror);
    if (added > 0 || removed > 0) {
        RCBN_LOG("Replication: prediction static mirror rescan +" << added << " -" << removed
                  << " (total " << m_predictionStaticMirror.size() << ")");
    }
}
