// Polyhymnia - Edit window: toolbar, rulers, track headers, clips, MIDI notes, automation (after SoundCraft crates/ui-egui/src/toolbar.rs & edit_window/*.rs).
#pragma once

#include "playback.h"
#include "session.h"

namespace Edit {

struct DropTarget {
    bool inTimeline = false;
    uint64_t trackNumId = 0; // 0 = empty space (create new track)
    long long sampleAt = 0;
};

void drawToolbar(Session::Document& doc);
void draw(Session::Document& doc, float bottomReserve = 0.0f);
void drawModals(Session::Document& doc);

void openFadesDialog();
void scriptSetup(Session::Document& doc);
float samplesPerPixel();
void setSamplesPerPixel(float spp);
long long sampleAtX(float localX, float headerW);
void openSpotForScript(Session::Document& doc);
DropTarget hitTestDrop(const Session::Document& doc, float screenX, float screenY);

} // namespace Edit
