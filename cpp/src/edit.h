// Polyhymnia - Edit window timeline, rulers and waveforms (after SoundCraft ui-egui edit view).
#pragma once
#include "session.h"
#include "playback.h"

namespace Edit {

struct DropTarget {
    bool inTimeline = false;
    uint64_t trackNumId = 0; // 0 = empty space (create new track)
    long long sampleAt = 0;
};

void draw(Session::Document& doc, float transportReserve);
void drawToolbar();
void scriptSetup(Session::Document& doc);
float samplesPerPixel();
long long sampleAtX(float localX, float headerW);
void openSpotForScript(Session::Document& doc);
DropTarget hitTestDrop(const Session::Document& doc, float screenX, float screenY);

} // namespace Edit