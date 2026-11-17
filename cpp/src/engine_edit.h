// Editing algorithms on playlists (after SoundCraft crates/engine/src/edit.rs).
#pragma once
#include "session.h"
#include "time_range.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EngEdit {

extern const char* SNAP_TO_GRID;
extern const char* SHUFFLE_LOCK;
extern const char* RELATIVE_GRID;
extern const char* NO_ROOM;
extern const char* IN_CROSSFADE;

struct TimeCtx {
    const Time::Map* tempo = nullptr;
    double sampleRate = 48000.0;
    static TimeCtx of(const Session::Document& s);
    long long ticks(long long samples) const;
};

struct PasteOutcome {
    std::vector<std::string> ids;
    const char* error = nullptr;
};

struct ShuffleOutcome {
    size_t count = 0;
    const char* error = nullptr;
};

// after SoundCraft (crates/engine/src/edit.rs split_clip)
std::optional<std::pair<Session::Clip, Session::Clip>> splitClip(const Session::Clip& clip, long long at, const std::string& newId,
                                                                 const TimeCtx& t);

// after SoundCraft (crates/engine/src/edit.rs separate_at)
size_t separateAt(Session::Document& s, const std::string& track, long long at);

// after SoundCraft (crates/engine/src/edit.rs separate_at_if)
size_t separateAtIf(Session::Document& s, const std::string& track, long long at, const std::function<bool(const Session::Clip&)>& autoName);

// after SoundCraft (crates/engine/src/edit.rs separate_all_at)
size_t separateAllAt(Session::Document& s, const std::string& track, long long at);

// after SoundCraft (crates/engine/src/edit.rs separate_at_points)
size_t separateAtPoints(Session::Document& s, const std::string& track, std::vector<long long> points);

// after SoundCraft (crates/engine/src/edit.rs auto_name_new_clips)
void autoNameNewClips(Session::Document& s, const std::string& track, const std::unordered_set<std::string>& old,
                      const std::function<bool(const Session::Clip&)>& autoName);

// after SoundCraft (crates/engine/src/edit.rs clear_range)
std::vector<Session::Clip> clearRange(Session::Document& s, const std::string& track, Time::Range range, bool shuffle);

// after SoundCraft (crates/engine/src/edit.rs remove_clips)
size_t removeClips(Session::Document& s, const std::vector<std::string>& ids, bool shuffle);

// after SoundCraft (crates/engine/src/edit.rs copy_range)
std::vector<Session::Clip> copyRange(const Session::Document& s, const std::string& track, Time::Range range);

// after SoundCraft (crates/engine/src/edit.rs consolidate_midi)
bool consolidateMidi(Session::Document& s, const std::string& track, Time::Range range);

// after SoundCraft (crates/engine/src/edit.rs paste)
PasteOutcome paste(Session::Document& s, const std::string& track, long long at, const std::vector<Session::Clip>& clips, long long length,
                   bool shuffle);

// after SoundCraft (crates/engine/src/edit.rs place_clip)
void placeClip(Session::Document& s, const std::string& track, Session::Clip clip);

// after SoundCraft (crates/engine/src/edit.rs insert_silence)
void insertSilence(Session::Document& s, const std::string& track, Time::Range range);

// after SoundCraft (crates/engine/src/edit.rs shift_from)
void shiftFrom(Session::Document& s, const std::string& track, long long from, long long by);

// after SoundCraft (crates/engine/src/edit.rs locked)
bool locked(const Session::Clip& c);

// after SoundCraft (crates/engine/src/edit.rs shuffle_room)
long long shuffleRoom(const Session::Document& s, const std::string& track, long long at);

// after SoundCraft (crates/engine/src/edit.rs make_room)
const char* makeRoom(Session::Document& s, const std::string& track, long long at, long long len);

// after SoundCraft (crates/engine/src/edit.rs trim_to)
size_t trimTo(Session::Document& s, const std::string& track, Time::Range range);

// after SoundCraft (crates/engine/src/edit.rs trim_start_to)
size_t trimStartTo(Session::Document& s, const std::string& track, long long at);

// after SoundCraft (crates/engine/src/edit.rs trim_end_to)
size_t trimEndTo(Session::Document& s, const std::string& track, long long at);

// after SoundCraft (crates/engine/src/edit.rs heal)
size_t heal(Session::Document& s, const std::string& track, Time::Range range);

// after SoundCraft (crates/engine/src/edit.rs move_clips)
size_t moveClips(Session::Document& s, const std::vector<std::string>& ids, long long delta,
                 const std::vector<std::pair<std::string, std::string>>& dest);

// after SoundCraft (crates/engine/src/edit.rs shown)
bool shown(const Session::Document& s, const Session::Track& t);

// after SoundCraft (crates/engine/src/edit.rs destinations)
std::optional<std::vector<std::pair<std::string, std::string>>> destinations(const Session::Document& s, const std::vector<std::string>& ids,
                                                                             const std::string& from, const std::string& to);

// after SoundCraft (crates/engine/src/edit.rs lands)
std::string lands(const std::vector<std::pair<std::string, std::string>>& dest, const std::string& t);

// after SoundCraft (crates/engine/src/edit.rs first_movable)
const Session::Clip* firstMovable(const Session::Document& s, const std::vector<std::string>& ids);

// after SoundCraft (crates/engine/src/edit.rs shuffle_slots)
std::vector<long long> shuffleSlots(const Session::Document& s, const std::string& track, const std::vector<std::string>& moving);

// after SoundCraft (crates/engine/src/edit.rs nearest_slot)
long long nearestSlot(const std::vector<long long>& slots, long long at);

// after SoundCraft (crates/engine/src/edit.rs shuffle_slot)
long long shuffleSlot(const Session::Document& s, const std::string& track, long long at, const std::vector<std::string>& moving);

// after SoundCraft (crates/engine/src/edit.rs shuffle_groups)
std::vector<std::pair<std::string, long long>> shuffleGroups(const Session::Document& s, const std::vector<std::string>& ids,
                                                              const std::vector<std::pair<std::string, std::string>>& dest);

// after SoundCraft (crates/engine/src/edit.rs shuffle_clips)
ShuffleOutcome shuffleClips(Session::Document& s, const std::vector<std::string>& ids, long long delta,
                            const std::vector<std::pair<std::string, std::string>>& dest);

// after SoundCraft (crates/engine/src/edit.rs grid_delta)
long long gridDelta(const Session::Document& s, const Session::Clip& clip, long long raw, bool relative, const std::vector<std::string>& moving);

// after SoundCraft (crates/engine/src/edit.rs trim_clip)
std::optional<Session::Clip> trimClip(Session::Document& s, const std::string& id, std::optional<long long> start, std::optional<long long> end,
                                      bool shuffle);

// after SoundCraft (crates/engine/src/edit.rs create_fades)
size_t createFades(Session::Document& s, const std::string& track, Time::Range range, Session::FadeShape shape);

// after SoundCraft (crates/engine/src/edit.rs delete_fades)
size_t deleteFades(Session::Document& s, const std::string& track, Time::Range range);

} // namespace EngEdit