// Spot mode time entry (after SoundCraft crates/ui-egui/src/dialogs/spot.rs).
#pragma once
#include "session.h"
#include "time_format.h"

#include <optional>
#include <string>
#include <vector>

namespace Spot {

enum class TargetKind { Clips, TrimStart, TrimEnd, Source };

struct State {
    TargetKind target = TargetKind::Clips;
    std::vector<std::string> clipIds;
    std::string fromTrack;
    std::optional<std::string> toTrack;
    std::string trimClip;
    bool trimStart = true;
    long long start = 0;
    long long len = 0;
    long long sync = 0;
    std::optional<long long> stamp;
    TimeFormat::Kind format = TimeFormat::Kind::MinSecs;
    std::string text[3];
    bool fresh = false;
};

bool isOpen();
void close();

// after spot.rs Spot::clips
std::optional<State> clips(const Session::Document& s, std::vector<std::string> clipIds, std::optional<std::string> track, long long delta);

// after spot.rs Spot::trim
std::optional<State> trim(const Session::Document& s, const std::string& clipId, bool startEdge, long long to);

void open(State s);

// Draw modal fields; returns true when closed.
bool draw(Session::Document& doc);
bool apply(const State& st, Session::Document& doc);

} // namespace Spot