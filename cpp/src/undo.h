// Polyhymnia - document undo stack (whole snapshots, after Chimera doc History).
#pragma once
#include "session.h"

#include <string>
#include <vector>

namespace Undo {

struct History {
    std::vector<Session::Document> undoStack;
    std::vector<Session::Document> redoStack;
    std::vector<std::string> undoLabels;
    std::vector<std::string> redoLabels;
    static constexpr size_t kMax = 80;

    void clear();
    bool canUndo() const;
    bool canRedo() const;
    const std::string& undoLabel() const;
    const std::string& redoLabel() const;
    void pushBefore(const Session::Document& doc, const std::string& label);
    bool doUndo(Session::Document& doc);
    bool doRedo(Session::Document& doc);
};

} // namespace Undo