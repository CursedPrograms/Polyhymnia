// Ports of audio import, source, and clip list tests (after SoundCraft crates/model/src/source.rs,
// crates/engine/src/io/import.rs, crates/engine/src/cmd/file/tests.rs, crates/engine/src/cmd/clip_list.rs,
// crates/ui-egui/src/import.rs, crates/ui-egui/src/panels/clips.rs).
#include "clips_panel.h"
#include "engine.h"
#include "engine_edit.h"
#include "import_audio.h"
#include "test_runner.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string toneWav(const fs::path& dir, const char* name, size_t nFrames = 4800, int chans = 1) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<float> tone(nFrames);
    for (size_t i = 0; i < nFrames; ++i) {
        tone[i] = std::sin(static_cast<float>(i) * 0.1f) * 0.2f;
    }
    Session::AudioBuffer buf;
    buf.sampleRate = 48000;
    buf.channels.assign(static_cast<size_t>(chans), tone);
    const auto bytes = ImportAudio::encodeWav(buf, 24);
    const fs::path p = dir / name;
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return p.u8string();
}

void settleImports(ImportAudio::Imports& imports, EngCmd::Engine& engine, std::string& status) {
    for (int i = 0; i < 2000; ++i) {
        imports.step(engine, status);
        if (!imports.busy()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

std::pair<uint64_t, std::vector<uint64_t>> findKickSourceAndClips(const EngCmd::Engine& e) {
    const auto& s = e.session();
    uint64_t f = 0;
    for (const auto& src : s.sources) {
        if (src.name == "Kick" || src.path.find("Kick") != std::string::npos) {
            f = src.numId;
            break;
        }
    }
    std::vector<uint64_t> clips;
    for (const auto* c : Session::clipsUsing(s, f)) {
        clips.push_back(c->numId);
    }
    return {f, clips};
}

} // namespace

// after SoundCraft (crates/model/src/source.rs the_format_names_in_session_files_do_not_change)
static bool test_the_format_names_in_session_files_do_not_change() {
    using Session::FileFormat;
    const std::pair<FileFormat, const char*> cases[] = {
        {FileFormat::Wav, "Wav"},   {FileFormat::Aiff, "Aiff"}, {FileFormat::Flac, "Flac"},
        {FileFormat::Mp3, "Mp3"},   {FileFormat::Ogg, "Ogg"},   {FileFormat::Aac, "Aac"},
        {FileFormat::Alac, "Alac"}, {FileFormat::Caf, "Caf"},   {FileFormat::Other, "Other"},
    };
    for (const auto& [fmt, name] : cases) {
        EXPECT_EQ(std::string(Session::fileFormatName(fmt)), std::string(name));
        const auto parsed = Session::fileFormatFromName(name);
        EXPECT_TRUE(parsed.has_value());
        EXPECT_TRUE(*parsed == fmt);
    }
    return true;
}

// after SoundCraft (crates/model/src/source.rs an_mp4_is_named_by_its_codec)
static bool test_an_mp4_is_named_by_its_codec() {
    using Session::FileFormat;
    const std::tuple<const char*, const char*, FileFormat> cases[] = {
        {"Mp4", "Aac", FileFormat::Aac},
        {"Mp4", "HeAac", FileFormat::Aac},
        {"QuickTime", "Alac", FileFormat::Alac},
        {"Mp4", "Opus", FileFormat::Other},
        {"Mp4", "Ac3", FileFormat::Other},
        {"QuickTime", "Pcm", FileFormat::Other},
        {"Adts", "Aac", FileFormat::Aac},
    };
    for (const auto& [containerName, codecName, expected] : cases) {
        ImportAudio::AudioInfo info;
        const auto cont = ImportAudio::containerFromName(containerName);
        const auto cod = ImportAudio::codecFromName(codecName);
        EXPECT_TRUE(cont.has_value());
        EXPECT_TRUE(cod.has_value());
        info.container = *cont;
        info.codec = *cod;
        EXPECT_TRUE(ImportAudio::fileFormatOf(info) == expected);
    }
    return true;
}

// after SoundCraft (crates/model/src/source.rs a_probed_file_maps_to_its_format)
static bool test_a_probed_file_maps_to_its_format() {
    Session::AudioBuffer buf;
    buf.sampleRate = 48000;
    buf.channels = {std::vector<float>(480, 0.25f)};

    const auto wav = ImportAudio::encodeWav(buf, 24);
    const auto aiff = ImportAudio::encodeAiff(buf, 16);
    const auto flac = ImportAudio::encodeFlac(buf);

    for (const auto& [bytes, expected] : std::vector<std::pair<std::vector<uint8_t>, Session::FileFormat>>{
             {wav, Session::FileFormat::Wav},
             {aiff, Session::FileFormat::Aiff},
             {flac, Session::FileFormat::Flac}}) {
        ImportAudio::AudioInfo info;
        std::string err;
        EXPECT_TRUE(ImportAudio::probeBytes(bytes.data(), bytes.size(), std::nullopt, info, err));
        EXPECT_TRUE(ImportAudio::fileFormatOf(info) == expected);
    }
    return true;
}

// after SoundCraft (crates/engine/src/io/import.rs a_tiny_sample_rate_is_refused_before_it_is_converted)
static bool test_a_tiny_sample_rate_is_refused_before_it_is_converted() {
    auto makeBuf = [](uint32_t rate) {
        Session::AudioBuffer b;
        b.sampleRate = rate;
        b.channels = {std::vector<float>(100, 0.5f), std::vector<float>(100, 0.5f)};
        return b;
    };
    EXPECT_TRUE(ImportAudio::atRate(makeBuf(1), 48000, 1000000).second.has_value());
    const auto ok = ImportAudio::atRate(makeBuf(8000), 48000, 1000000);
    EXPECT_TRUE(ok.first.has_value());
    EXPECT_EQ(ok.first->sampleRate, 48000u);
    EXPECT_EQ(ok.first->frames(), 600u);
    return true;
}

// after SoundCraft (crates/engine/src/io/import.rs an_extreme_rate_expansion_is_refused_whatever_the_sample_budget)
static bool test_an_extreme_rate_expansion_is_refused_whatever_the_sample_budget() {
    auto makeBuf = [](uint32_t rate) {
        Session::AudioBuffer b;
        b.sampleRate = rate;
        b.channels = {std::vector<float>(100, 0.5f), std::vector<float>(100, 0.5f)};
        return b;
    };
    const auto r1 = ImportAudio::atRate(makeBuf(300), 192000, SIZE_MAX);
    EXPECT_TRUE(r1.second.has_value());
    EXPECT_TRUE(*r1.second == ImportAudio::RateError::ratio(300, 192000));
    EXPECT_TRUE(ImportAudio::atRate(makeBuf(1500), 192000, SIZE_MAX).first.has_value());
    EXPECT_TRUE(ImportAudio::atRate(makeBuf(8000), 768000, SIZE_MAX).first.has_value());
    const auto r2 = ImportAudio::atRate(makeBuf(1490), 192000, SIZE_MAX);
    EXPECT_TRUE(r2.second.has_value());
    EXPECT_TRUE(*r2.second == ImportAudio::RateError::ratio(1490, 192000));
    const auto r3 = ImportAudio::atRate(makeBuf(48000), 48000, 10);
    EXPECT_TRUE(r3.second.has_value());
    EXPECT_TRUE(*r3.second == ImportAudio::RateError::tooLarge());
    EXPECT_TRUE(ImportAudio::RateError::ratio(300, 192000).message("big").find("300 Hz") != std::string::npos);
    return true;
}

// after SoundCraft (crates/engine/src/cmd/file/tests.rs import_audio_places_audio_offered_by_a_worker)
static bool test_import_audio_places_audio_offered_by_a_worker() {
    const fs::path dir = fs::temp_directory_path() / "poly_sc_import_offered";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string wav = toneWav(dir, "tone.wav", 4800, 1);

    EngCmd::Engine e = EngCmd::Engine::fresh();
    const uint32_t rate = static_cast<uint32_t>(e.session().sampleRate);
    std::pair<std::optional<ImportAudio::DecodedImport>, std::string> dec;
    std::thread worker([&]() { dec = ImportAudio::decodeImport(wav, rate); });
    worker.join();
    EXPECT_TRUE(dec.first.has_value());
    EXPECT_EQ(dec.first->frames(), 4800u);
    e.offerImport(*dec.first);

    {
        std::ofstream bad(fs::u8path(wav), std::ios::binary | std::ios::trunc);
        bad << "not audio";
    }
    EngCmd::Engine::Params p;
    p.strs["path"] = wav;
    p.nums["at"] = 960;
    const auto r = e.execute("file.import_audio", p);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.nums.at("frames"), 4800.0);
    EXPECT_EQ(e.session().tracks.size(), 1u);
    EXPECT_EQ(e.session().tracks[0].clips[0].start, 960LL);
    EXPECT_EQ(e.session().sources[0].path, wav);

    // Taken once: the next import reads the broken file and fails.
    EngCmd::Engine::Params p2;
    p2.strs["path"] = wav;
    EXPECT_TRUE(!e.execute("file.import_audio", p2).ok);

    // Decoded for another rate: ignored, so the broken file is read and refused.
    const std::string good = toneWav(dir, "good.wav", 4800, 1);
    const auto other = ImportAudio::decodeImport(good, rate / 2);
    EXPECT_TRUE(other.first.has_value());
    {
        std::ofstream bad(fs::u8path(good), std::ios::binary | std::ios::trunc);
        bad << "not audio";
    }
    e.offerImport(*other.first);
    EngCmd::Engine::Params p3;
    p3.strs["path"] = good;
    EXPECT_TRUE(!e.execute("file.import_audio", p3).ok);
    e.dropOfferedImports();
    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/engine/src/cmd/file/tests.rs import_audio_honours_new_tracks)
static bool test_import_audio_honours_new_tracks() {
    const fs::path dir = fs::temp_directory_path() / "poly_sc_import_new_tracks";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string wav = toneWav(dir, "tone.wav", 4800, 1);

    // Default / new_tracks:true: create a new track even when one is selected.
    {
        EngCmd::Engine e = EngCmd::Engine::fresh();
        EngCmd::Engine::Params tn;
        tn.nums["count"] = 1;
        tn.strs["name"] = "Target";
        tn.strs["format"] = "mono";
        EXPECT_TRUE(e.execute("track.new", tn).ok);
        const uint64_t target = e.session().tracks[0].numId;
        e.sessionMut().sessionEdit.selectedTracks = {target};
        EngCmd::Engine::Params imp;
        imp.strs["path"] = wav;
        imp.bools["new_tracks"] = true;
        EXPECT_TRUE(e.execute("file.import_audio", imp).ok);
        EXPECT_EQ(e.session().tracks.size(), 2u);
        EXPECT_EQ(Session::findTrackByNum(e.session(), target)->clips.size(), 0u);
        EXPECT_EQ(e.session().tracks[1].clips.size(), 1u);
    }

    // new_tracks:false with a selected audio track: import onto it.
    {
        EngCmd::Engine e = EngCmd::Engine::fresh();
        EngCmd::Engine::Params tn;
        tn.nums["count"] = 1;
        tn.strs["name"] = "Target";
        tn.strs["format"] = "mono";
        EXPECT_TRUE(e.execute("track.new", tn).ok);
        const uint64_t target = e.session().tracks[0].numId;
        e.sessionMut().sessionEdit.selectedTracks = {target};
        EngCmd::Engine::Params imp;
        imp.strs["path"] = wav;
        imp.bools["new_tracks"] = false;
        EXPECT_TRUE(e.execute("file.import_audio", imp).ok);
        EXPECT_EQ(e.session().tracks.size(), 1u);
        EXPECT_EQ(Session::findTrackByNum(e.session(), target)->clips.size(), 1u);
    }

    // new_tracks:false with no selection and no track: clear error.
    {
        EngCmd::Engine e = EngCmd::Engine::fresh();
        EngCmd::Engine::Params imp;
        imp.strs["path"] = wav;
        imp.bools["new_tracks"] = false;
        const auto r = e.execute("file.import_audio", imp);
        EXPECT_TRUE(!r.ok);
        EXPECT_TRUE(r.error.find("new_tracks: false") != std::string::npos);
    }

    // Explicit track wins over new_tracks:true.
    {
        EngCmd::Engine e = EngCmd::Engine::fresh();
        EngCmd::Engine::Params tn;
        tn.nums["count"] = 1;
        tn.strs["name"] = "Target";
        tn.strs["format"] = "mono";
        EXPECT_TRUE(e.execute("track.new", tn).ok);
        EngCmd::Engine::Params imp;
        imp.strs["path"] = wav;
        imp.strs["track"] = "Target";
        imp.bools["new_tracks"] = true;
        EXPECT_TRUE(e.execute("file.import_audio", imp).ok);
        EXPECT_EQ(e.session().tracks.size(), 1u);
        EXPECT_EQ(e.session().tracks[0].clips.size(), 1u);
    }

    // Omitted new_tracks still creates a track on a blank session.
    {
        EngCmd::Engine e = EngCmd::Engine::fresh();
        EngCmd::Engine::Params imp;
        imp.strs["path"] = wav;
        EXPECT_TRUE(e.execute("file.import_audio", imp).ok);
        EXPECT_EQ(e.session().tracks.size(), 1u);
        EXPECT_TRUE(e.session().tracks[0].kind == Session::TrackKind::Audio);
        EXPECT_TRUE(e.session().tracks[0].format == Session::ChannelFormat::Mono);
    }

    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/engine/src/cmd/file/tests.rs import_surround_creates_a_matching_track)
static bool test_import_surround_creates_a_matching_track() {
    Session::AudioBuffer buf;
    buf.sampleRate = 48000;
    for (int ch = 0; ch < 6; ++ch) {
        buf.channels.push_back(std::vector<float>(32, 0.1f * static_cast<float>(ch + 1)));
    }
    const auto bytes = ImportAudio::encodeWav(buf, 24);
    EngCmd::Engine e = EngCmd::Engine::fresh();
    const auto v = ImportAudio::importAudioBytes(e.sessionMut(), "bed.wav", bytes.data(), bytes.size(), std::nullopt, std::nullopt, 0);
    EXPECT_TRUE(v.ok);
    EXPECT_EQ(v.nums.at("channels"), 6.0);
    EXPECT_EQ(e.session().tracks.size(), 1u);
    EXPECT_TRUE(e.session().tracks[0].format == Session::ChannelFormat::Surround51);
    return true;
}

// after SoundCraft (crates/engine/src/cmd/clip_list.rs clearing_a_clip_takes_it_off_the_timeline_and_keeps_its_file)
static bool test_clearing_a_clip_takes_it_off_the_timeline_and_keeps_its_file() {
    EngCmd::Engine e = EngCmd::Engine::demo();
    const auto [f, clips] = findKickSourceAndClips(e);
    EXPECT_TRUE(!clips.empty());
    EngCmd::Engine::Params p;
    p.idLists["clips"] = {clips[0]};
    const auto r = e.execute("clip.clear", p);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.nums.at("clips"), 1.0);
    EXPECT_EQ(r.nums.at("sources"), 0.0);
    EXPECT_TRUE(!Session::findClipByNum(e.session(), clips[0]).has_value());
    EXPECT_TRUE(Session::findSourceByNum(e.session(), f) != nullptr);
    EXPECT_TRUE(e.undo());
    EXPECT_TRUE(Session::findClipByNum(e.session(), clips[0]).has_value());
    return true;
}

// after SoundCraft (crates/engine/src/cmd/clip_list.rs a_file_in_use_stays_unless_its_clips_go_too)
static bool test_a_file_in_use_stays_unless_its_clips_go_too() {
    EngCmd::Engine e = EngCmd::Engine::demo();
    EngCmd::Engine::Params pd;
    pd.strs["track"] = "Kick";
    EXPECT_TRUE(e.execute("track.playlist_duplicate", pd).ok);
    const auto [f, clips] = findKickSourceAndClips(e);
    EXPECT_EQ(clips.size(), 6u);
    EngCmd::Engine::Params p1;
    p1.idLists["sources"] = {f};
    const auto err = e.execute("clip.clear", p1);
    EXPECT_TRUE(!err.ok);
    EXPECT_TRUE(err.error.find("used by 6 clips") != std::string::npos);

    EngCmd::Engine::Params p2;
    p2.idLists["sources"] = {f};
    p2.bools["with_clips"] = true;
    const auto r = e.execute("clip.clear", p2);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.nums.at("clips"), 6.0);
    EXPECT_EQ(r.nums.at("sources"), 1.0);
    EXPECT_TRUE(Session::findSourceByNum(e.session(), f) == nullptr && !e.session().pool.contains(f));
    EXPECT_EQ(Session::clipsUsing(e.session(), f).size(), 0u);
    EXPECT_TRUE(e.undoLabel() == std::optional<std::string>("Clear Clips"));
    EXPECT_TRUE(e.undo());
    EXPECT_TRUE(e.session().pool.contains(f));
    EXPECT_EQ(Session::clipsUsing(e.session(), f).size(), 6u);
    return true;
}

// after SoundCraft (crates/engine/src/cmd/clip_list.rs listing_a_files_clips_too_is_enough)
static bool test_listing_a_files_clips_too_is_enough() {
    EngCmd::Engine e = EngCmd::Engine::demo();
    const auto [f, clips] = findKickSourceAndClips(e);
    EngCmd::Engine::Params p;
    p.idLists["clips"] = clips;
    p.idLists["sources"] = {f};
    const auto r = e.execute("clip.clear", p);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.nums.at("clips"), 3.0);
    EXPECT_EQ(r.nums.at("sources"), 1.0);
    return true;
}

// after SoundCraft (crates/engine/src/cmd/clip_list.rs the_clip_list_selection_is_the_default_target)
static bool test_the_clip_list_selection_is_the_default_target() {
    EngCmd::Engine e = EngCmd::Engine::demo();
    Session::AudioBuffer buf;
    buf.sampleRate = 48000;
    buf.channels = {std::vector<float>(4800, 0.25f)};
    const uint64_t f = ImportAudio::addSource(e.sessionMut(), "Spare", std::move(buf), std::nullopt, Session::FileFormat::Wav);
    const auto [_, clips] = findKickSourceAndClips(e);

    {
        EngCmd::Engine::Params s1;
        s1.idLists["clips"] = {clips[0]};
        EXPECT_TRUE(e.execute("edit.select", s1).ok);
    }
    {
        EngCmd::Engine::Params s2;
        s2.idLists["sources"] = {f, f, 999999};
        const auto r = e.execute("edit.select", s2);
        EXPECT_TRUE(r.ok);
        EXPECT_EQ(e.session().sessionEdit.selectedSources, std::vector<uint64_t>{f});
        EXPECT_TRUE(e.session().sessionEdit.selectedClips.empty());
    }
    {
        EngCmd::Engine::Params s3;
        s3.idLists["clips"] = {clips[0]};
        const auto r = e.execute("edit.select", s3);
        EXPECT_TRUE(r.ok);
        EXPECT_TRUE(e.session().sessionEdit.selectedSources.empty());
    }
    EXPECT_TRUE(e.execute("clip.clear", {}).ok);
    EXPECT_TRUE(!Session::findClipByNum(e.session(), clips[0]).has_value());
    EXPECT_TRUE(e.session().sessionEdit.selectedClips.empty());

    {
        EngCmd::Engine::Params s4;
        s4.idLists["sources"] = {f};
        EXPECT_TRUE(e.execute("edit.select", s4).ok);
    }
    EXPECT_TRUE(e.execute("clip.clear", {}).ok);
    EXPECT_TRUE(Session::findSourceByNum(e.session(), f) == nullptr);
    EXPECT_TRUE(e.session().sessionEdit.selectedSources.empty());
    return true;
}

// after SoundCraft (crates/engine/src/cmd/clip_list.rs bad_targets_are_errors)
static bool test_bad_targets_are_errors() {
    EngCmd::Engine e = EngCmd::Engine::demo();
    EXPECT_TRUE(!e.execute("clip.clear", {}).ok);
    {
        EngCmd::Engine::Params p;
        p.idLists["clips"] = {};
        EXPECT_TRUE(!e.execute("clip.clear", p).ok);
    }
    {
        EngCmd::Engine::Params p;
        p.idLists["clips"] = {999999};
        EXPECT_TRUE(!e.execute("clip.clear", p).ok);
    }
    {
        EngCmd::Engine::Params p;
        p.idLists["sources"] = {999999};
        EXPECT_TRUE(!e.execute("clip.clear", p).ok);
    }
    {
        EngCmd::Engine::Params p;
        p.strs["sources"] = "Kick";
        EXPECT_TRUE(!e.execute("clip.clear", p).ok);
    }
    return true;
}

// after SoundCraft (crates/engine/src/cmd/clip_list.rs deselect_all_clears_the_clip_list_selection)
static bool test_deselect_all_clears_the_clip_list_selection() {
    EngCmd::Engine e = EngCmd::Engine::demo();
    const auto [f, _] = findKickSourceAndClips(e);
    EngCmd::Engine::Params p;
    p.idLists["sources"] = {f};
    EXPECT_TRUE(e.execute("edit.select", p).ok);
    EXPECT_TRUE(e.execute("edit.select_none", {}).ok);
    EXPECT_TRUE(e.session().sessionEdit.selectedSources.empty());
    return true;
}

// after SoundCraft (crates/ui-egui/src/import.rs import_runs_in_the_background_and_places_one_undoable_clip)
static bool test_import_runs_in_the_background_and_places_one_undoable_clip() {
    const fs::path dir = fs::temp_directory_path() / "poly_sc_ui_import";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string wav = toneWav(dir, "bg.wav", 9600, 2);

    EngCmd::Engine engine = EngCmd::Engine::fresh();
    ImportAudio::Imports imports;
    std::string status;
    EngCmd::Engine::Params p;
    p.strs["path"] = wav;
    p.nums["at"] = 480;
    imports.start(engine, status, p);
    EXPECT_TRUE(imports.busy());
    EXPECT_TRUE(engine.session().tracks.empty());
    settleImports(imports, engine, status);
    const auto& s = engine.session();
    EXPECT_EQ(s.tracks.size(), 1u);
    EXPECT_EQ(s.tracks[0].clips[0].start, 480LL);
    EXPECT_EQ(s.sources[0].frames, 9600LL);
    EXPECT_TRUE(status.rfind("Imported bg.wav", 0) == 0);
    EXPECT_TRUE(engine.execute("edit.undo", {}).ok);
    EXPECT_TRUE(engine.session().tracks.empty());
    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/ui-egui/src/import.rs imports_queue_and_a_bad_file_reports_without_stopping_the_rest)
static bool test_imports_queue_and_a_bad_file_reports_without_stopping_the_rest() {
    const fs::path dir = fs::temp_directory_path() / "poly_sc_ui_import_queue";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string junk = toneWav(dir, "junk.wav", 9600, 2);
    {
        std::ofstream bad(fs::u8path(junk), std::ios::binary | std::ios::trunc);
        bad << "not audio";
    }
    const std::string good = toneWav(dir, "good.wav", 9600, 2);

    EngCmd::Engine engine = EngCmd::Engine::fresh();
    ImportAudio::Imports imports;
    std::string status;
    EngCmd::Engine::Params p1, p2;
    p1.strs["path"] = junk;
    p2.strs["path"] = good;
    imports.start(engine, status, p1);
    imports.start(engine, status, p2);
    imports.step(engine, status);
    EXPECT_TRUE(imports.label().has_value() && imports.label()->second == 1u);
    settleImports(imports, engine, status);
    EXPECT_EQ(engine.session().tracks.size(), 1u);
    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/ui-egui/src/import.rs a_failed_import_stays_on_the_status_line_while_the_next_one_starts)
static bool test_a_failed_import_stays_on_the_status_line_while_the_next_one_starts() {
    const fs::path dir = fs::temp_directory_path() / "poly_sc_ui_import_status";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string junk = toneWav(dir, "junk-status.wav", 9600, 2);
    {
        std::ofstream bad(fs::u8path(junk), std::ios::binary | std::ios::trunc);
        bad << "not audio";
    }
    const std::string next = toneWav(dir, "next-status.wav", 9600, 2);

    EngCmd::Engine engine = EngCmd::Engine::fresh();
    ImportAudio::Imports imports;
    std::string status;
    EngCmd::Engine::Params p1, p2;
    p1.strs["path"] = junk;
    p2.strs["path"] = next;
    imports.start(engine, status, p1);
    imports.start(engine, status, p2);
    for (int i = 0; i < 2000; ++i) {
        imports.step(engine, status);
        if (imports.label().has_value() && imports.label()->first == "next-status.wav") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(imports.label().has_value() && imports.label()->first == "next-status.wav");
    EXPECT_TRUE(!status.empty() && status.rfind("Importing", 0) != 0);
    settleImports(imports, engine, status);
    fs::remove_all(dir, ec);
    return true;
}

// after SoundCraft (crates/ui-egui/src/panels/clips.rs clip_rows_list_every_clip_in_track_order_and_filter_by_name)
static bool test_clip_rows_list_every_clip_in_track_order_and_filter_by_name() {
    Session::Document s = Session::empty();
    const char* groups[2][2] = {{"Kick 1", "Snare 1"}, {"Kick 2", "Tom"}};
    for (int g = 0; g < 2; ++g) {
        const uint64_t tNum = Session::addTrack(s, Session::TrackKind::Audio, Session::ChannelFormat::Mono);
        Session::Track* tr = Session::findTrackByNum(s, tNum);
        for (int i = 0; i < 2; ++i) {
            Session::Clip c;
            c.numId = Session::newClipNumId(s);
            c.id = "clip_" + std::to_string(c.numId);
            c.name = groups[g][i];
            c.sourceId = "src_1";
            c.start = i * 100;
            c.length = 100;
            tr->clips.push_back(c);
        }
    }
    auto names = [&](const std::vector<ClipsPanel::ClipRow>& rows) {
        std::vector<std::string> out;
        for (const auto& r : rows) {
            if (r.kind == ClipsPanel::ClipRow::Kind::Clip) {
                out.push_back(s.tracks[r.trackIdx].activeClips()[r.clipIdx].name);
            }
        }
        return out;
    };
    const std::vector<std::string> expectedAll = {"Kick 1", "Snare 1", "Kick 2", "Tom"};
    const std::vector<std::string> expectedKick = {"Kick 1", "Kick 2"};
    EXPECT_EQ(names(ClipsPanel::clipRows(s, "")), expectedAll);
    EXPECT_EQ(names(ClipsPanel::clipRows(s, "kick")), expectedKick);
    EXPECT_TRUE(ClipsPanel::clipRows(s, "cowbell").empty());
    return true;
}

void registerImportAudioTests() {
    Test::add("source::the_format_names_in_session_files_do_not_change", test_the_format_names_in_session_files_do_not_change);
    Test::add("source::an_mp4_is_named_by_its_codec", test_an_mp4_is_named_by_its_codec);
    Test::add("source::a_probed_file_maps_to_its_format", test_a_probed_file_maps_to_its_format);
    Test::add("import::a_tiny_sample_rate_is_refused_before_it_is_converted", test_a_tiny_sample_rate_is_refused_before_it_is_converted);
    Test::add("import::an_extreme_rate_expansion_is_refused_whatever_the_sample_budget", test_an_extreme_rate_expansion_is_refused_whatever_the_sample_budget);
    Test::add("file::import_audio_places_audio_offered_by_a_worker", test_import_audio_places_audio_offered_by_a_worker);
    Test::add("file::import_audio_honours_new_tracks", test_import_audio_honours_new_tracks);
    Test::add("file::import_surround_creates_a_matching_track", test_import_surround_creates_a_matching_track);
    Test::add("clip_list::clearing_a_clip_takes_it_off_the_timeline_and_keeps_its_file", test_clearing_a_clip_takes_it_off_the_timeline_and_keeps_its_file);
    Test::add("clip_list::a_file_in_use_stays_unless_its_clips_go_too", test_a_file_in_use_stays_unless_its_clips_go_too);
    Test::add("clip_list::listing_a_files_clips_too_is_enough", test_listing_a_files_clips_too_is_enough);
    Test::add("clip_list::the_clip_list_selection_is_the_default_target", test_the_clip_list_selection_is_the_default_target);
    Test::add("clip_list::bad_targets_are_errors", test_bad_targets_are_errors);
    Test::add("clip_list::deselect_all_clears_the_clip_list_selection", test_deselect_all_clears_the_clip_list_selection);
    Test::add("ui_import::import_runs_in_the_background_and_places_one_undoable_clip", test_import_runs_in_the_background_and_places_one_undoable_clip);
    Test::add("ui_import::imports_queue_and_a_bad_file_reports_without_stopping_the_rest", test_imports_queue_and_a_bad_file_reports_without_stopping_the_rest);
    Test::add("ui_import::a_failed_import_stays_on_the_status_line_while_the_next_one_starts", test_a_failed_import_stays_on_the_status_line_while_the_next_one_starts);
    Test::add("clips_panel::clip_rows_list_every_clip_in_track_order_and_filter_by_name", test_clip_rows_list_every_clip_in_track_order_and_filter_by_name);
}
