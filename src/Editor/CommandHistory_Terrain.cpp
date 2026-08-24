#include <Editor/CommandHistory.hpp>

SetTerrainBoolCommand::SetTerrainBoolCommand(std::shared_ptr<Terrain> target, std::string prop,
                                             bool before, bool after)
    : m_target(std::move(target)), m_prop(std::move(prop)), m_before(before), m_after(after) {}
void SetTerrainBoolCommand::execute() { apply(m_after); }
void SetTerrainBoolCommand::undo() { apply(m_before); }
void SetTerrainBoolCommand::apply(bool v) {
    if (!m_target) return;
    if (m_prop == "Enabled") m_target->setEnabled(v);
    else if (m_prop == "Flat") m_target->Flat = v;
}

SetTerrainIntCommand::SetTerrainIntCommand(std::shared_ptr<Terrain> target, std::string prop,
                                           int before, int after)
    : m_target(std::move(target)), m_prop(std::move(prop)), m_before(before), m_after(after) {}
void SetTerrainIntCommand::execute() { apply(m_after); }
void SetTerrainIntCommand::undo() { apply(m_before); }
void SetTerrainIntCommand::apply(int v) {
    if (m_target && m_prop == "Seed") m_target->Seed = v;
}

SetTerrainStringCommand::SetTerrainStringCommand(std::shared_ptr<Terrain> target, std::string prop,
                                                 std::string before, std::string after)
    : m_target(std::move(target)), m_prop(std::move(prop)),
      m_before(std::move(before)), m_after(std::move(after)) {}
void SetTerrainStringCommand::execute() { apply(m_after); }
void SetTerrainStringCommand::undo() { apply(m_before); }
void SetTerrainStringCommand::apply(const std::string& v) {
    if (!m_target || m_prop != "DataPath") return;
    YAML::Node n; n = v; m_target->setProperty("DataPath", n);
}

TerrainBrushStrokeCommand::TerrainBrushStrokeCommand(std::shared_ptr<Terrain> target,
    std::vector<TerrainStreamer::VoxelDiffEntry> entries)
    : m_target(std::move(target)), m_entries(std::move(entries)) {}
void TerrainBrushStrokeCommand::execute() { apply(true); }
void TerrainBrushStrokeCommand::undo() { apply(false); }
void TerrainBrushStrokeCommand::apply(bool useAfter) {
    if (!m_target || !m_target->streamer) return;
    for (auto& e : m_entries) {
        const Block& v = useAfter ? e.after : e.before;
        m_target->streamer->setBlock(e.wx, e.wy, e.wz, v.shape, v.r, v.g, v.b);
    }
}
