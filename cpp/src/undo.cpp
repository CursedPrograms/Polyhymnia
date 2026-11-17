// Polyhymnia - whole-document undo snapshots.
#include "undo.h"

namespace Undo {

void History::clear() {
    undoStack.clear();
    redoStack.clear();
    undoLabels.clear();
    redoLabels.clear();
}

bool History::canUndo() const { return !undoStack.empty(); }
bool History::canRedo() const { return !redoStack.empty(); }

const std::string& History::undoLabel() const {
    static const std::string none;
    return canUndo() ? undoLabels.back() : none;
}

const std::string& History::redoLabel() const {
    static const std::string none;
    return canRedo() ? redoLabels.back() : none;
}

void History::pushBefore(const Session::Document& doc, const std::string& label) {
    undoStack.push_back(doc);
    undoLabels.push_back(label);
    if (undoStack.size() > kMax) {
        undoStack.erase(undoStack.begin());
        undoLabels.erase(undoLabels.begin());
    }
    redoStack.clear();
    redoLabels.clear();
}

bool History::doUndo(Session::Document& doc) {
    if (!canUndo()) return false;
    redoStack.push_back(doc);
    redoLabels.push_back(undoLabels.back());
    doc = undoStack.back();
    undoStack.pop_back();
    undoLabels.pop_back();
    return true;
}

bool History::doRedo(Session::Document& doc) {
    if (!canRedo()) return false;
    undoStack.push_back(doc);
    undoLabels.push_back(redoLabels.back());
    doc = redoStack.back();
    redoStack.pop_back();
    redoLabels.pop_back();
    return true;
}

} // namespace Undo