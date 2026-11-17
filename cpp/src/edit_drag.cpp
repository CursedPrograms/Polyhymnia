// Clip drag snapping and Spot vs immediate commit (after SoundCraft edit_window/drag.rs).
#include "edit_drag.h"
#include "editor.h"
#include "engine_edit.h"
#include "clip_ops.h"
#include "time_grid.h"
#include "time_range.h"

#include <algorithm>
#include <utility>
#include <unordered_set>

namespace EditDrag {

static bool gridMode() {
    const auto m = Editor::mode();
    return m == Editor::Mode::Grid || m == Editor::Mode::GridRelative;
}

long long dragSnap(const Session::Document& doc, long long at, bool flipCtrl) { return dragSnapExcluding(doc, at, flipCtrl, {}); }

long long dragSnapExcluding(const Session::Document& doc, long long at, bool flipCtrl, const std::vector<std::string>& excluded) {
    const bool gridOn = gridMode();
    if (gridOn != flipCtrl) return TimeGrid::snapToGridExcluding(doc, at, excluded);
    return at;
}

long long trimDragSnap(const Session::Document& doc, const std::string& clipId, long long at, bool start, bool flipCtrl) {
    if (auto fc = Session::findClip(doc, clipId)) {
        const long long origin = start ? fc->second->start : ClipOps::end(*fc->second);
        if (origin == at) return at;
    }
    return dragSnapExcluding(doc, at, flipCtrl, {clipId});
}

std::shared_ptr<const DragPlan> dragPlan(const Session::Document& doc, uint32_t touch, const std::vector<std::string>& clips, const std::string& from,
                                         const std::string& to, long long grabAt) {
    static uint32_t cachedTouch = 0;
    static Editor::Mode cachedMode = Editor::Mode::Slip;
    static std::vector<std::string> cachedClips;
    static std::string cachedFrom, cachedTo;
    static long long cachedGrab = 0;
    static std::shared_ptr<const DragPlan> cachedPlan;

    const Editor::Mode mode = Editor::mode();
    if (cachedPlan && cachedTouch == touch && cachedMode == mode && cachedFrom == from && cachedTo == to && cachedGrab == grabAt &&
        cachedClips == clips)
        return cachedPlan;

    auto plan = std::make_shared<DragPlan>();
    if (to == from)
        plan->dest = std::vector<std::pair<std::string, std::string>>{};
    else
        plan->dest = EngEdit::destinations(doc, clips, from, to);

    if (plan->dest && mode == Editor::Mode::Shuffle) {
        const auto groups = EngEdit::shuffleGroups(doc, clips, *plan->dest);
        for (const auto& pr : groups) {
            plan->groups.emplace_back(pr.first, pr.second, EngEdit::shuffleSlots(doc, pr.first, clips));
        }
    }

    std::unordered_set<std::string> ids(clips.begin(), clips.end());
    for (const auto& t : doc.tracks) {
        for (const auto& c : t.clips) {
            if (ids.count(c.id) && !ClipOps::locked(c)) {
                plan->moving.emplace_back(t.id, c.start, c.length);
                plan->movingIds.push_back(c.id);
            }
        }
    }

    const Session::Track* fromTr = Session::findTrack(doc, from);
    const Session::Clip* grabbed = nullptr;
    if (fromTr) {
        for (auto it = fromTr->clips.rbegin(); it != fromTr->clips.rend(); ++it) {
            if (!ids.count(it->id) || ClipOps::locked(*it)) continue;
            const Time::Range rgn = Time::Range::newRange(it->start, ClipOps::end(*it));
            if (rgn.contains(grabAt)) {
                grabbed = &*it;
                break;
            }
        }
        if (!grabbed) {
            for (const auto& c : fromTr->clips) {
                if (ids.count(c.id) && !ClipOps::locked(c)) {
                    if (!grabbed || c.start < grabbed->start) grabbed = &c;
                }
            }
        }
    }
    if (grabbed) plan->grabbed = *grabbed;

    cachedTouch = touch;
    cachedMode = mode;
    cachedClips = clips;
    cachedFrom = from;
    cachedTo = to;
    cachedGrab = grabAt;
    cachedPlan = plan;
    return plan;
}

long long moveDelta(const DragPlan& plan, const Session::Document& doc, const std::string& from, long long raw, bool flipCtrl) {
    const bool trackChanged = plan.dest.has_value() && EngEdit::lands(*plan.dest, from) != from;
    if (raw == 0 && !trackChanged) return 0;
    const Editor::Mode mode = Editor::mode();
    if (mode == Editor::Mode::Shuffle) {
        const std::string to = plan.dest ? EngEdit::lands(*plan.dest, from) : from;
        for (const auto& g : plan.groups) {
            if (std::get<0>(g) != to) continue;
            const long long first = std::get<1>(g);
            const auto& slots = std::get<2>(g);
            return Time::satSub(EngEdit::nearestSlot(slots, Time::satAdd(first, raw)), first);
        }
        return raw;
    }
    if (gridMode() != flipCtrl && plan.grabbed) {
        return EngEdit::gridDelta(doc, *plan.grabbed, raw, mode == Editor::Mode::GridRelative, plan.movingIds);
    }
    return raw;
}

std::vector<std::string> grabbedFirst(const Session::Document& doc, const std::vector<std::string>& clips, const std::string& from) {
    std::vector<std::string> v = clips;
    std::sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b) {
        const auto fa = Session::findClip(doc, a);
        const auto fb = Session::findClip(doc, b);
        const bool aLater = !fa || fa->first != from;
        const bool bLater = !fb || fb->first != from;
        return aLater < bLater;
    });
    return v;
}

void spotOrRun(Session::Document& doc, std::optional<Spot::State> spot) {
    if (!spot) return;
    if (Editor::mode() == Editor::Mode::Spot)
        Spot::open(std::move(*spot));
    else
        Spot::apply(*spot, doc);
}

} // namespace EditDrag