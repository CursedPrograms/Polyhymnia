// Grabber drag plan and spot commit (after SoundCraft edit_window/drag.rs).
#pragma once
#include "session.h"
#include "spot_dialog.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace EditDrag {

struct DragPlan {
    std::optional<std::vector<std::pair<std::string, std::string>>> dest;
    std::vector<std::tuple<std::string, long long, std::vector<long long>>> groups;
    std::vector<std::tuple<std::string, long long, long long>> moving;
    std::vector<std::string> movingIds;
    std::optional<Session::Clip> grabbed;
};

long long dragSnap(const Session::Document& doc, long long at, bool flipCtrl);
long long dragSnapExcluding(const Session::Document& doc, long long at, bool flipCtrl, const std::vector<std::string>& excluded);
long long trimDragSnap(const Session::Document& doc, const std::string& clipId, long long at, bool start, bool flipCtrl);

std::shared_ptr<const DragPlan> dragPlan(const Session::Document& doc, uint32_t touch, const std::vector<std::string>& clips, const std::string& from,
                                         const std::string& to, long long grabAt);

long long moveDelta(const DragPlan& plan, const Session::Document& doc, const std::string& from, long long raw, bool flipCtrl);

std::vector<std::string> grabbedFirst(const Session::Document& doc, const std::vector<std::string>& clips, const std::string& from);

// Spot mode opens the dialog; other edit modes commit immediately.
void spotOrRun(Session::Document& doc, std::optional<Spot::State> spot);

} // namespace EditDrag