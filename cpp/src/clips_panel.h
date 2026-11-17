// Polyhymnia - Clips list panel on the right (after SoundCraft crates/ui-egui/src/panels/clips.rs).
#pragma once

#include "session.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ClipsPanel {

// after SoundCraft (crates/ui-egui/src/state.rs DragSource)
constexpr const char* kDragSourcePayload = "POLY_DRAG_SOURCE";

// after SoundCraft (crates/ui-egui/src/panels/clips.rs ClipRow)
struct ClipRow {
    enum class Kind { Source, Clip };
    Kind kind = Kind::Source;
    size_t sourceIdx = 0;
    size_t trackIdx = 0;
    size_t clipIdx = 0;

    static ClipRow source(size_t i) { return {Kind::Source, i, 0, 0}; }
    static ClipRow clip(size_t tr, size_t cl) { return {Kind::Clip, 0, tr, cl}; }
};

// after SoundCraft (crates/ui-egui/src/panels/clips.rs clip_rows)
std::vector<ClipRow> clipRows(const Session::Document& s, const std::string& needle);

// after SoundCraft (crates/ui-egui/src/dialogs.rs clear_message)
std::optional<std::string> clearMessage(const Session::Document& s, const std::vector<uint64_t>& clips,
                                        const std::vector<uint64_t>& sources);

// Draw the docked CLIPS panel on the right of the Edit window.
void draw(Session::Document& doc, float width);

// Draw modal dialogs owned by the Clips list (Clear Clips, Rename Clip).
void drawDialogs(Session::Document& doc);

// Hit-test whether screen coordinates (x, y) are inside the Clips list panel.
bool containsScreenPoint(float x, float y);

} // namespace ClipsPanel
