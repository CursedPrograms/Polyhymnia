// Polyhymnia - transport and lock-free playhead for the audio thread (after SoundCraft playback).
#pragma once
#include "session.h"

namespace Playback {
void setDocument(const Session::Document* doc);
void play();
void stop();
void toggleLoop();
bool isPlaying();
bool loopEnabled();
long long playhead();
void setPlayhead(long long sample);
void fillOutput(float* stereo, unsigned frames, double sampleRate);
void advanceUi();
}