#include <Editor/CommandHistory.hpp>

Command::~Command() = default;

void CommandHistory::setOnChange(std::function<void()> cb) { m_onChange = std::move(cb); }
void CommandHistory::notifyChanged() { if (m_onChange) m_onChange(); }
bool CommandHistory::canUndo() const { return !m_undoStack.empty(); }
bool CommandHistory::canRedo() const { return !m_redoStack.empty(); }

void CommandHistory::execute(std::unique_ptr<Command> cmd) {
    cmd->execute();
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
    if (m_onChange) m_onChange();
}

void CommandHistory::record(std::unique_ptr<Command> cmd) {
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
    if (m_onChange) m_onChange();
}

void CommandHistory::undo() {
    if (m_undoStack.empty()) return;
    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    cmd->undo();
    m_redoStack.push_back(std::move(cmd));
    if (m_onChange) m_onChange();
}

void CommandHistory::redo() {
    if (m_redoStack.empty()) return;
    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    cmd->execute();
    m_undoStack.push_back(std::move(cmd));
    if (m_onChange) m_onChange();
}

void CommandHistory::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}
