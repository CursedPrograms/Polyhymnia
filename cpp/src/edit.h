// Polyhymnia - Edit window timeline, rulers and waveforms (after SoundCraft ui-egui edit view).
#pragma once
#include "session.h"
#include "playback.h"

namespace Edit {
void draw(Session::Document& doc, float transportReserve);
void drawToolbar();
void scriptSetup(Session::Document& doc);
float samplesPerPixel();
long long sampleAtX(float localX, float headerW);
void openSpotForScript(Session::Document& doc);
} // namespace Edit