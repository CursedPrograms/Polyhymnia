// Polyhymnia - procedural icons ported 1-to-1 from SoundCraft (after SoundCraft crates/ui-egui/src/icons.rs & toolbar.rs).
#pragma once

#include "session.h"
#include "time_grid.h"

#include <imgui.h>

#include <array>
#include <string>

namespace Icons {

// after SoundCraft (crates/ui-egui/src/icons.rs draw)
void draw(ImDrawList* dl, const ImVec2& rMin, const ImVec2& rMax, const char* name, ImU32 color);

// after SoundCraft (crates/ui-egui/src/widgets.rs icon_button)
bool iconButton(const char* id, const ImVec2& size, const char* iconName, bool active, const char* tooltip = nullptr,
                ImU32 activeBg = IM_COL32(0x00, 0x7b, 0xb7, 255));

// after SoundCraft (crates/ui-egui/src/toolbar.rs draw_note_value_icon)
void drawNoteValueIcon(ImDrawList* dl, const ImVec2& rMin, const ImVec2& rMax, TimeGrid::NoteValue value,
                       bool dotted, bool triplet, ImU32 color);

// after SoundCraft (crates/ui-egui/src/toolbar.rs compact_grid_label)
std::string compactGridLabel(const Session::Document& session, const TimeGrid::GridValue& value);

// after SoundCraft (crates/ui-egui/src/toolbar.rs option_toggles)
struct OptionToggleSpec {
    const char* commandId;
    const char* icon;
    bool on;
    const char* tooltip;
};

std::array<OptionToggleSpec, 6> optionToggles(const Session::Document& doc);

} // namespace Icons
