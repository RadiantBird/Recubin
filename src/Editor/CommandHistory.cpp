#include <Editor/CommandHistory.hpp>

void CommandHistory::execute(std::unique_ptr<Command> cmd) {
    cmd->execute();
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
}

void CommandHistory::record(std::unique_ptr<Command> cmd) {
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
}

void CommandHistory::undo() {
    if (m_undoStack.empty()) return;
    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    cmd->undo();
    m_redoStack.push_back(std::move(cmd));
}

void CommandHistory::redo() {
    if (m_redoStack.empty()) return;
    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    cmd->execute();
    m_undoStack.push_back(std::move(cmd));
}

void CommandHistory::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}
