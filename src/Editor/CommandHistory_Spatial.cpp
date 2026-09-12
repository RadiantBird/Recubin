#include <Editor/CommandHistory.hpp>

RecalculateSpatialCoordinatesCommand::RecalculateSpatialCoordinatesCommand(
    std::vector<Entry> entries) : m_entries(std::move(entries)) {}

void RecalculateSpatialCoordinatesCommand::execute() { apply(true); }
void RecalculateSpatialCoordinatesCommand::undo() { apply(false); }

void RecalculateSpatialCoordinatesCommand::apply(bool after) {
    // Apply all locals through the transaction path. Restoring never depends
    // on a partially updated parent or on the order in which children visit.
    std::vector<std::pair<Spatial*, CFrame>> batch;
    batch.reserve(m_entries.size());
    for (const auto& entry : m_entries) {
        if (!entry.target) continue;
        batch.emplace_back(entry.target.get(), after ? entry.afterLocal : entry.beforeLocal);
    }
    Spatial::applyLocalCFrameBatch(batch);
}
