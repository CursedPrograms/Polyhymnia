// Polyhymnia - Mix window: channel strips, inserts, sends, routing, faders, meters, and plugin editor windows
// (after SoundCraft crates/ui-egui/src/mix_window.rs, mix_window/*.rs, panels/plugins.rs, widgets.rs).
#pragma once

#include "session.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace MixWindow {

constexpr size_t kInstrumentSlot = 100;

float stripWidth(bool narrow);
std::optional<float> parseDb(const std::string& text);
std::optional<float> parsePan(const std::string& text);
float draggedSendDb(float db, float dy, bool fine);
std::string panText(float v);
std::string dbText(float db);
float meterPos(float db);
float levelPos(float v);

std::string routeName(const Session::Document& doc, const Session::Route& r);
std::string inputName(const Session::Document& doc, const Session::Track& track);

void setNarrowMix(bool narrow);
bool isNarrowMix();
void toggleNarrowMix();

void openPluginWindow(uint64_t trackNumId, size_t slot);
void closeAllPluginWindows();
void openSendWindow(uint64_t trackNumId, size_t slot);

void draw(Session::Document& doc);
void drawFloatingWindows(Session::Document& doc);

} // namespace MixWindow
