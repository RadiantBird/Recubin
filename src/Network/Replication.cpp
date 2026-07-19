#include <Network/Replication.hpp>
#include <Network/NetworkManager.hpp>
#include <Network/ByteStream.hpp>

#include <Instances/Workspace.hpp>
#include <Core/User.hpp>
#include <Instances/Model.hpp>
#include <Instances/BaseCube.hpp>
#include <Util/Logger.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

ReplicationManager::ReplicationManager(std::shared_ptr<Workspace> workspace, std::shared_ptr<User> user, Instance* characterSearchRoot)
    : m_workspace(workspace), m_user(user), m_characterSearchRoot(characterSearchRoot) {
}

void ReplicationManager::update(float dt) {
    auto& net = NetworkManager::get();
    if (!net.isActive()) return;

    reconcileAvatars();
    sendAvatarUpdates(dt);
    applyAvatarPoses(dt);

    if (net.getRole() == NetworkRole::Host) {
        hostUpdateWorld(dt);
    } else if (net.getRole() == NetworkRole::Client) {
        clientApplyWorldSmoothing(dt);
    }
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
        if (!net.hasPeers()) return;
        ByteWriter w;
        w.writeU8(static_cast<uint8_t>(MessageType::AvatarState));
        w.writeVector3(local.Position);
        w.writeQuat(local.Rotation);
        net.sendBytes(w.data, NetworkChannel::Unreliable);
    } else if (net.getRole() == NetworkRole::Host) {
        m_latestPoses[net.getLocalPeerId()] = local;

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
            w.writeU32(id);
            w.writeVector3(pose.Position);
            w.writeQuat(pose.Rotation);
        }
        net.sendBytes(w.data, NetworkChannel::Unreliable);
    }
}

void ReplicationManager::onGameMessage(uint8_t type, const uint8_t* payload, size_t len, PeerId senderId) {
    if (type == static_cast<uint8_t>(MessageType::AvatarState)) {
        ByteReader r{payload, len};
        Vector3 pos;
        Quaternion rot;
        if (!r.readVector3(pos) || !r.readQuat(rot)) return;
        m_latestPoses[senderId] = CFrame(pos, rot);
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
            if (!r.readU32(id) || !r.readVector3(pos) || !r.readQuat(rot)) return;
            if (id == localId) continue;
            m_latestPoses[id] = CFrame(pos, rot);
        }
    } else if (type == static_cast<uint8_t>(MessageType::WorldMapping)) {
        if (NetworkManager::get().getRole() == NetworkRole::Client) {
            clientApplyWorldMapping(payload, len);
        }
    } else if (type == static_cast<uint8_t>(MessageType::WorldTransforms)) {
        if (NetworkManager::get().getRole() == NetworkRole::Client) {
            clientStoreWorldTransforms(payload, len);
        }
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

static void collectSyncTargets(const std::shared_ptr<Instance>& node, Instance* excludeCharacter,
                                std::vector<std::shared_ptr<BaseCube>>& out) {
    if (excludeCharacter && node.get() == excludeCharacter) return;
    if (node->Name.rfind("RemotePlayer_", 0) == 0) return;

    auto cube = std::dynamic_pointer_cast<BaseCube>(node);
    if (cube && !cube->Anchored && cube->CanCollide) out.push_back(cube);

    for (auto& [name, child] : node->getChildren()) {
        collectSyncTargets(child, excludeCharacter, out);
    }
}

void ReplicationManager::spawnRemoteAvatar(PeerId id) {
    auto model = User::buildCharacterModel(m_characterSearchRoot, "RemotePlayer_" + std::to_string(id));
    if (!model) {
        RCBN_LOG("Replication: failed to build remote avatar model for peer " << id);
        return;
    }

    model->removeChild("Humanoid");

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
    Quaternion invR = root->cframe.Rotation.conjugate();
    for (BaseCube* cube : cubes) {
        CFrame rel;
        rel.Position = invR.rotate(cube->cframe.Position - root->cframe.Position);
        rel.Rotation = invR * cube->cframe.Rotation;
        avatar.parts.emplace_back(cube, rel);
    }

    m_workspace->addChild(model);
    m_remoteAvatars[id] = std::move(avatar);

    RCBN_LOG("Replication: spawned RemotePlayer_" << id);
}

void ReplicationManager::despawnRemoteAvatar(PeerId id) {
    auto it = m_remoteAvatars.find(id);
    if (it == m_remoteAvatars.end()) return;

    m_workspace->removeChild(it->second.model->Name);
    m_latestPoses.erase(id);
    m_remoteAvatars.erase(it);

    RCBN_LOG("Replication: despawned RemotePlayer_" << id);
}

void ReplicationManager::despawnAllAvatars() {
    std::vector<PeerId> ids;
    for (const auto& [id, avatar] : m_remoteAvatars) ids.push_back(id);
    for (PeerId id : ids) despawnRemoteAvatar(id);
}

void ReplicationManager::applyAvatarPoses(float dt) {
    for (auto& [id, avatar] : m_remoteAvatars) {
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
}

void ReplicationManager::hostRescanWorld() {
    Instance* characterPtr = (m_user && m_user->character) ? m_user->character.get() : nullptr;

    std::vector<std::shared_ptr<BaseCube>> targets;
    for (auto& [name, child] : m_workspace->getChildren()) {
        collectSyncTargets(child, characterPtr, targets);
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
        if (obj.tailTimer > 0.0f) obj.tailTimer -= 0.05f;

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
    if (newRole == NetworkRole::Offline) {
        despawnAllAvatars();
        m_latestPoses.clear();
        clientReleaseWorldObjects();
        m_pathToNetId.clear();
        m_hostObjects.clear();
        m_worldMappingDirty = false;
        m_prevRosterSize = 0;
    } else if (oldRole == NetworkRole::Client && newRole == NetworkRole::Host) {
        // ホスト昇格: クライアントとして凍結(Anchored化)していた物理を解放し、自分の世界を権威として再スキャンさせる
        clientReleaseWorldObjects();
        m_worldMappingDirty = true;
        m_worldRescanTimer = 1.0f; // 次のupdateで即再スキャン
    }
}
