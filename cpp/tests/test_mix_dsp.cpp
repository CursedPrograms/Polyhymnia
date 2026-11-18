// Polyhymnia - Mix window, Mixer model, and DSP unit tests
// after SoundCraft (crates/dsp/src/biquad.rs, crates/dsp/src/lib.rs, crates/model/src/mixer.rs, crates/ui-egui/src/mix_window.rs, crates/mix/src/tests.rs).
#include "dsp.h"
#include "mix.h"
#include "mix_window.h"
#include "session.h"
#include "test_runner.h"

#include <cmath>
#include <limits>
#include <vector>

namespace {

static bool approx(float a, float b, float eps = 0.05f) {
    return std::fabs(a - b) <= eps;
}

// after SoundCraft (crates/dsp/src/biquad.rs peak_gain_at_center)
static bool testPeakGainAtCenter() {
    const auto c = Dsp::Coeffs::design(Dsp::FilterType::Peak, 1000.0f, 1.0f, 6.0f, 48000.0f);
    EXPECT_TRUE(std::fabs(c.magnitudeDb(1000.0f, 48000.0f) - 6.0f) < 0.05f);
    EXPECT_TRUE(std::fabs(c.magnitudeDb(50.0f, 48000.0f)) < 0.05f);
    return true;
}

// after SoundCraft (crates/dsp/src/biquad.rs lowpass_minus3_at_cutoff)
static bool testLowpassMinus3AtCutoff() {
    const auto c = Dsp::Coeffs::design(Dsp::FilterType::LowPass, 2000.0f, 0.707f, 0.0f, 48000.0f);
    EXPECT_TRUE(std::fabs(c.magnitudeDb(2000.0f, 48000.0f) + 3.0f) < 0.1f);
    return true;
}

// after SoundCraft (crates/dsp/src/biquad.rs hostile_inputs)
static bool testHostileInputs() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto c = Dsp::Coeffs::design(Dsp::FilterType::Peak, nan, -5.0f, inf, 0.0f);
    EXPECT_TRUE(std::isfinite(c.b0));
    EXPECT_TRUE(std::isfinite(c.magnitudeDb(nan, 0.0f)));
    return true;
}

// after SoundCraft (crates/dsp/src/lib.rs built_ins_have_no_state_or_editor)
static bool testBuiltInsHaveNoStateOrEditor() {
    EXPECT_TRUE(!Dsp::plugins().empty());
    for (const auto* info : Dsp::plugins()) {
        EXPECT_TRUE(info != nullptr);
        auto p = Dsp::create(info->id);
        EXPECT_TRUE(p != nullptr);
        p->prepare(48000.0f, 64, 2);
        std::vector<std::vector<float>> buf = {std::vector<float>(64, 0.1f), std::vector<float>(64, -0.1f)};
        p->process(buf, 64);
        EXPECT_TRUE(std::isfinite(buf[0][32]) && std::isfinite(buf[1][32]));
    }
    return true;
}

// after SoundCraft (crates/dsp/src/plugins/dynamics.rs compress_gr)
static bool testCompressGr() {
    EXPECT_TRUE(std::fabs(Dsp::compressGr(-30.0f, -18.0f, 4.0f, 0.0f)) < 1e-4f);
    EXPECT_TRUE(approx(Dsp::compressGr(-6.0f, -18.0f, 4.0f, 0.0f), 9.0f, 0.05f));
    return true;
}

// after SoundCraft (crates/model/src/mixer.rs fader_taper_round_trips, formats)
static bool testFaderTaperRoundTrips() {
    const float samples[] = {-100.0f, -60.0f, -24.0f, -10.0f, 0.0f, 6.0f, 12.0f};
    for (float db : samples) {
        const float pos = Session::faderDbToPos(db);
        const float back = Session::faderPosToDb(pos);
        EXPECT_TRUE(approx(db, back, 0.05f));
    }
    EXPECT_TRUE(Session::faderPosToDb(0.0f) == -144.0f);
    EXPECT_TRUE(Session::faderDbToPos(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    EXPECT_EQ(Session::channelFormatForChannels(6), Session::ChannelFormat::Surround51);
    EXPECT_TRUE(Session::channelFormatFromId("stereo") == Session::ChannelFormat::Stereo);
    return true;
}

// after SoundCraft (crates/ui-egui/src/mix_window.rs typed_db_accepts_console_notation_and_clamps)
static bool testTypedDbAcceptsConsoleNotationAndClamps() {
    auto p = [](const char* s) { return MixWindow::parseDb(s); };
    EXPECT_TRUE(p("-6 dB").has_value() && approx(*p("-6 dB"), -6.0f));
    EXPECT_TRUE(p("+3,5").has_value() && approx(*p("+3,5"), 3.5f));
    EXPECT_TRUE(p("off").has_value() && approx(*p("off"), -144.0f));
    EXPECT_TRUE(p("99").has_value() && approx(*p("99"), 12.0f));
    EXPECT_TRUE(!p("loud").has_value());
    return true;
}

// after SoundCraft (crates/ui-egui/src/mix_window.rs typed_pan_accepts_percent_console_and_fractional_values)
static bool testTypedPanAcceptsPercentConsoleAndFractionalValues() {
    auto p = [](const char* s) { return MixWindow::parsePan(s); };
    EXPECT_TRUE(p("-50").has_value() && approx(*p("-50"), -0.5f));
    EXPECT_TRUE(p("<35").has_value() && approx(*p("<35"), -0.35f));
    EXPECT_TRUE(p("35>").has_value() && approx(*p("35>"), 0.35f));
    EXPECT_TRUE(p("R25").has_value() && approx(*p("R25"), 0.25f));
    EXPECT_TRUE(p("0.5").has_value() && approx(*p("0.5"), 0.5f));
    EXPECT_TRUE(!p("center").has_value());
    return true;
}

// after SoundCraft (crates/ui-egui/src/mix_window.rs send_drags_follow_the_fader_taper_down_to_minus_infinity)
static bool testSendDragsFollowTheFaderTaperDownToMinusInfinity() {
    EXPECT_TRUE(std::fabs(MixWindow::draggedSendDb(0.0f, -10.0f, false) - 1.44f) < 0.01f);
    EXPECT_TRUE(std::fabs(MixWindow::draggedSendDb(0.0f, -10.0f, true) - 0.192f) < 0.01f);
    EXPECT_TRUE(MixWindow::draggedSendDb(0.0f, 400.0f, false) == -144.0f);
    const float up = MixWindow::draggedSendDb(-144.0f, -1.0f, false);
    EXPECT_TRUE(up > -144.0f && up < -100.0f);
    EXPECT_TRUE(MixWindow::draggedSendDb(12.0f, -50.0f, false) == 12.0f);
    return true;
}

// after SoundCraft (crates/ui-egui/src/mix_window/routing.rs stereo_track_defaults_to_a_stereo_input_pair)
static bool testStereoTrackDefaultsToAStereoInputPair() {
    const auto doc = Session::empty();
    Session::Track t;
    t.id = "t1";
    t.name = "Audio 1";
    t.kind = Session::TrackKind::Audio;
    t.format = Session::ChannelFormat::Stereo;
    EXPECT_EQ(MixWindow::inputName(doc, t), std::string("In 1-2"));
    t.format = Session::ChannelFormat::Mono;
    EXPECT_EQ(MixWindow::inputName(doc, t), std::string("In 1"));
    t.kind = Session::TrackKind::Aux;
    EXPECT_EQ(MixWindow::inputName(doc, t), std::string("no output"));
    return true;
}

// after SoundCraft (crates/mix/src/tests.rs inserts_and_bus_send_render_through_aux_and_master)
static bool testInsertsAndBusSendRenderThroughAuxAndMaster() {
    Session::Document doc = Session::empty();
    doc.sampleRate = 48000.0;
    const uint64_t busId = Session::addBus(doc, "Verb", Session::ChannelFormat::Stereo);

    Session::Source src;
    src.id = "s1";
    src.numId = 1;
    src.name = "Tone";
    src.sampleRate = 48000;
    src.channels = 1;
    src.frames = 256;
    src.monoPcm.assign(256, 0.25f);
    doc.sources.push_back(src);

    Session::Track audio;
    audio.id = "t1";
    audio.numId = 1;
    audio.name = "Lead";
    audio.kind = Session::TrackKind::Audio;
    Session::Clip clip;
    clip.id = "c1";
    clip.numId = 1;
    clip.sourceId = "s1";
    clip.start = 0;
    clip.length = 256;
    audio.clips.push_back(clip);
    // Insert a +6 dB Gain plugin on slot 0.
    Session::Insert gainIns = Session::Insert::make("gain");
    gainIns.params["gain"] = 6.0f;
    audio.mixer.inserts[0] = gainIns;
    // Send pre-fader at 0 dB to busId, and mute the direct track output so only the Aux return is heard!
    audio.mute = true;
    Session::SendSlot send = Session::SendSlot::make(Session::Route::bus(busId), 0.0f);
    send.preFader = true;
    audio.mixer.sends[0] = send;
    doc.tracks.push_back(audio);

    Session::Track aux;
    aux.id = "t2";
    aux.numId = 2;
    aux.name = "Aux Return";
    aux.kind = Session::TrackKind::Aux;
    aux.mixer.input = Session::Route::bus(busId);
    aux.mixer.output = Session::Route::mainOut();
    doc.tracks.push_back(aux);

    Session::Track master;
    master.id = "t3";
    master.numId = 3;
    master.name = "Master";
    master.kind = Session::TrackKind::Master;
    master.mixer.inserts[0] = Session::Insert::make("invert");
    doc.tracks.push_back(master);

    Mix::rebuildPlan(doc);
    std::vector<float> out(128 * 2, 0.0f);
    Mix::render(0, out.data(), 128, 48000.0);
    // Input is +0.25, boosted +6 dB (~0.5) by track insert, sent pre-fader to Verb bus, returned by Aux, inverted by Master insert -> negative output!
    EXPECT_TRUE(out[10] < -0.2f);
    return true;
}

} // namespace

void registerMixDspTests() {
    Test::add("biquad_peak_gain_at_center", testPeakGainAtCenter);
    Test::add("biquad_lowpass_minus3_at_cutoff", testLowpassMinus3AtCutoff);
    Test::add("biquad_hostile_inputs", testHostileInputs);
    Test::add("dsp_built_ins_have_no_state_or_editor", testBuiltInsHaveNoStateOrEditor);
    Test::add("dsp_compress_gr", testCompressGr);
    Test::add("mixer_fader_taper_round_trips", testFaderTaperRoundTrips);
    Test::add("mix_window_typed_db_accepts_console_notation_and_clamps", testTypedDbAcceptsConsoleNotationAndClamps);
    Test::add("mix_window_typed_pan_accepts_percent_console_and_fractional_values", testTypedPanAcceptsPercentConsoleAndFractionalValues);
    Test::add("mix_window_send_drags_follow_the_fader_taper_down_to_minus_infinity", testSendDragsFollowTheFaderTaperDownToMinusInfinity);
    Test::add("mix_window_stereo_track_defaults_to_a_stereo_input_pair", testStereoTrackDefaultsToAStereoInputPair);
    Test::add("mix_inserts_and_bus_send_render_through_aux_and_master", testInsertsAndBusSendRenderThroughAuxAndMaster);
}
