// Polyhymnia - editing modes, tools and clip ops (after SoundCraft crates/engine edit).
#pragma once
#include "session.h"

#include <string>
#include <vector>

namespace Editor {

enum class Mode { Shuffle, Slip, Spot, Grid, GridRelative };

enum class Tool { Smart, Selector, Grabber, Trim };

// One chosen clip on a track.
struct ClipRef {
    std::string trackId;
    std::string clipId;
};

struct Selection {
    long long start = 0;
    long long end = 0;
    std::string trackId;
    std::vector<ClipRef> clips;
};

Mode mode();
Tool tool();
void setMode(Mode m);
void setTool(Tool t);
const char* modeLabel(Mode m);
const char* toolLabel(Tool t);

Selection& selection();
bool snapToGrid();
void setSnapToGrid(bool on);
long long gridStepSamples(const Session::Document& doc);
long long snapSample(long long s, const Session::Document& doc);

void selectClip(const Session::Document& doc, const std::string& trackId, const std::string& clipId);
void selectRange(long long start, long long end, const std::string& trackId);
void clearSelection();

bool hasClipboard();
void clearClipboard();

// Mutations return true when the document changed.
bool cut(Session::Document& doc);
bool copy(const Session::Document& doc);
bool paste(Session::Document& doc, long long at, const std::string& trackId);
bool duplicateSelection(Session::Document& doc);
bool separateAtPlayhead(Session::Document& doc, long long at);
bool healSeparation(Session::Document& doc);
bool consolidateSelection(Session::Document& doc);
bool nudgeSelection(Session::Document& doc, long long delta);

bool moveClip(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newStart);
bool trimClipStart(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newStart);
bool trimClipEnd(Session::Document& doc, const std::string& trackId, const std::string& clipId, long long newEnd);

void setClipGainDb(Session::Document& doc, const std::string& trackId, const std::string& clipId, float gainDb);
void setClipFade(Session::Document& doc, const std::string& trackId, const std::string& clipId, bool fadeIn, long long len,
                 Session::FadeShape shape);
void applyCrossfadeOverlap(Session::Document& doc, const std::string& trackId, const std::string& clipA, const std::string& clipB,
                           long long overlap);

void scriptDemo(Session::Document& doc);

} // namespace Editor