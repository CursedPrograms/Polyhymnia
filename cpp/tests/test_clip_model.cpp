// Ports of clip.rs mod tests (after SoundCraft crates/model/src/clip.rs).
#include "clip_ops.h"
#include "test_runner.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

float dbToGain(float db) {
    if (std::isnan(db) || db <= -144.0f) return 0.0f;
    return std::pow(10.0f, std::min(db, 48.0f) / 20.0f);
}

bool near(float a, float b, float eps) {
    if (std::abs(a - b) > eps) {
        std::fprintf(stderr, "  near failed: %g != %g (eps %g)\n", a, b, eps);
        return false;
    }
    return true;
}

Session::Clip audioClip(long long offset, long long start, long long len) {
    Session::Clip c;
    c.id = "c1";
    c.name = "a";
    c.sourceId = "s1";
    c.offset = offset;
    c.start = start;
    c.length = len;
    return c;
}

} // namespace

static bool test_fade_shapes_hit_endpoints() {
    const Session::FadeShape all[] = {Session::FadeShape::Linear, Session::FadeShape::EqualPower, Session::FadeShape::SCurve,
                                      Session::FadeShape::Exponential, Session::FadeShape::Logarithmic};
    for (Session::FadeShape s : all) {
        EXPECT_TRUE(ClipOps::fadeShapeGain(s, 0.0f) < 1e-6f);
        EXPECT_TRUE(near(ClipOps::fadeShapeGain(s, 1.0f), 1.0f, 1e-6f));
        EXPECT_EQ(ClipOps::fadeShapeGain(s, std::numeric_limits<float>::quiet_NaN()), 0.0f);
        auto parsed = ClipOps::fadeShapeFromId(ClipOps::fadeShapeLabel(s));
        EXPECT_TRUE(parsed.has_value());
        EXPECT_TRUE(parsed.value() == s);
    }
    return true;
}

static bool test_fade_curvature_is_bounded_monotonic_and_inverts_at_the_midpoint() {
    const Session::FadeShape shapes[] = {Session::FadeShape::Linear, Session::FadeShape::EqualPower, Session::FadeShape::SCurve,
                                         Session::FadeShape::Exponential, Session::FadeShape::Logarithmic};
    for (Session::FadeShape shape : shapes) {
        const Session::Fade original{1000, shape, 0.0f};
        for (int i = 0; i <= 128; ++i) {
            const float t = float(i) / 128.0f;
            EXPECT_TRUE(near(ClipOps::fadeGain(original, t), ClipOps::fadeShapeGain(shape, t), 1e-6f));
        }
        for (float curvature : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            const Session::Fade fade{1000, shape, curvature};
            EXPECT_EQ(ClipOps::fadeGain(fade, 0.0f), 0.0f);
            EXPECT_EQ(ClipOps::fadeGain(fade, 1.0f), 1.0f);
            float previous = 0.0f;
            for (int i = 0; i <= 128; ++i) {
                const float gain = ClipOps::fadeGain(fade, float(i) / 128.0f);
                EXPECT_TRUE(std::isfinite(gain) && gain >= 0.0f && gain <= 1.0f);
                EXPECT_TRUE(gain >= previous - 1e-6f);
                previous = gain;
            }
            EXPECT_TRUE(near(ClipOps::fadeCurvatureForMidpointGain(fade, ClipOps::fadeGain(fade, 0.5f)), curvature, 1e-5f));
        }
        Session::Fade up{1000, shape, 0.5f};
        Session::Fade down{1000, shape, -0.5f};
        EXPECT_TRUE(ClipOps::fadeGain(up, 0.5f) > ClipOps::fadeGain(original, 0.5f));
        EXPECT_TRUE(ClipOps::fadeGain(down, 0.5f) < ClipOps::fadeGain(original, 0.5f));
    }
    return true;
}

static bool test_fade_curvature_loads_legacy_data_and_sanitizes_hostile_values() {
    Session::Fade fade{100, Session::FadeShape::SCurve, 0.0f};
    EXPECT_EQ(fade.curvature, 0.0f);
    for (float curvature : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(), -10.0f, 10.0f}) {
        Session::Fade f = fade;
        f.curvature = curvature;
        EXPECT_TRUE(std::isfinite(ClipOps::fadeGain(f, 0.5f)));
        ClipOps::fadeSanitize(f);
        EXPECT_TRUE(std::isfinite(f.curvature) && f.curvature >= -1.0f && f.curvature <= 1.0f);
        Session::Fade roundtrip = f;
        ClipOps::fadeSanitize(roundtrip);
        EXPECT_EQ(roundtrip.len, f.len);
        EXPECT_TRUE(roundtrip.shape == f.shape);
        EXPECT_EQ(roundtrip.curvature, f.curvature);
    }
    return true;
}

static bool test_temporary_fades_keep_clip_gain_and_envelope_without_mutating_the_clip() {
    Session::Clip clip = audioClip(0, 0, 1000);
    clip.gainDb = -6.0f;
    clip.gainEnv = {{0, 0.0f}, {1000, 6.0f}};
    clip.fadeIn = {100, Session::FadeShape::SCurve, 0.25f};
    clip.fadeOut = {100, Session::FadeShape::Logarithmic, -0.1f};
    const Session::Clip before = clip;
    const Session::Fade fadeIn{1000, Session::FadeShape::Linear, 0.5f};
    const Session::Fade fadeOut{1000, Session::FadeShape::Linear, -0.5f};
    EXPECT_TRUE(near(ClipOps::gainAtWithFades(clip, 500, fadeIn, fadeOut), dbToGain(-3.0f) * 0.16f, 1e-4f));
    Session::Clip committed = clip;
    committed.fadeIn = fadeIn;
    committed.fadeOut = fadeOut;
    for (long long rel : {0LL, 50LL, 500LL, 999LL, 1000LL}) {
        EXPECT_EQ(ClipOps::gainAtWithFades(clip, rel, fadeIn, fadeOut), ClipOps::gainAt(committed, rel));
    }
    for (long long rel : {std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()}) {
        EXPECT_TRUE(std::isfinite(ClipOps::gainAt(clip, rel)));
        EXPECT_TRUE(std::isfinite(ClipOps::gainAtWithFades(clip, rel, fadeIn, fadeOut)));
    }
    EXPECT_EQ(clip.id, before.id);
    EXPECT_EQ(clip.gainDb, before.gainDb);
    EXPECT_EQ(clip.gainEnv, before.gainEnv);
    EXPECT_EQ(clip.fadeIn.len, before.fadeIn.len);
    EXPECT_EQ(clip.fadeOut.len, before.fadeOut.len);
    clip.gainEnv = {{std::numeric_limits<long long>::min(), 0.0f}, {std::numeric_limits<long long>::max(), 6.0f}};
    EXPECT_TRUE(near(ClipOps::gainAt(clip, 500), dbToGain(-3.0f), 1e-6f));
    EXPECT_TRUE(near(ClipOps::gainAtWithFades(clip, 500, fadeIn, fadeOut), dbToGain(-3.0f) * 0.16f, 1e-4f));
    return true;
}

static bool test_gain_with_fades() {
    Session::Clip c = audioClip(0, 100, 1000);
    c.fadeIn = {100, Session::FadeShape::Linear, 0.0f};
    c.fadeOut = {100, Session::FadeShape::Linear, 0.0f};
    EXPECT_TRUE(ClipOps::gainAt(c, 0) < 1e-6f);
    EXPECT_TRUE(near(ClipOps::gainAt(c, 50), 0.5f, 1e-6f));
    EXPECT_TRUE(near(ClipOps::gainAt(c, 500), 1.0f, 1e-6f));
    EXPECT_TRUE(near(ClipOps::gainAt(c, 950), 0.5f, 1e-6f));
    return true;
}

static bool test_gain_envelope_interpolates() {
    Session::Clip c = audioClip(0, 0, 1000);
    c.gainEnv = {{0, 0.0f}, {1000, -20.0f}};
    EXPECT_TRUE(near(ClipOps::gainAt(c, 500), dbToGain(-10.0f), 1e-4f));
    return true;
}

static bool test_trim_start_keeps_gain_envelope_at_cut() {
    const std::vector<std::tuple<std::vector<std::pair<long long, float>>, long long, std::vector<std::pair<long long, float>>>> cases = {
        {{{0, 0.0f}, {1000, -20.0f}}, 500, {{0, -10.0f}, {500, -20.0f}}},
        {{{0, 0.0f}, {100, -20.0f}}, 500, {{0, -20.0f}}},
        {{{0, 0.0f}, {500, -10.0f}, {1000, -20.0f}}, 500, {{0, -10.0f}, {500, -20.0f}}},
        {{}, 500, {}},
        {{{100, -3.0f}}, 0, {{100, -3.0f}}},
        {{{100, -3.0f}}, -100, {{200, -3.0f}}},
    };
    for (const auto& row : cases) {
        Session::Clip clip = audioClip(100, 0, 1000);
        clip.gainEnv = std::get<0>(row);
        const long long cut = std::get<1>(row);
        const float gainAtCut = ClipOps::gainAt(clip, cut);
        ClipOps::trimStartTo(clip, cut);
        EXPECT_EQ(clip.gainEnv, std::get<2>(row));
        if (cut > 0) EXPECT_TRUE(near(ClipOps::gainAt(clip, 0), gainAtCut, 1e-6f));
    }
    return true;
}

static bool test_trims_keep_material_anchored() {
    Session::Clip c = audioClip(1000, 100, 1000);
    ClipOps::trimStartTo(c, 300);
    EXPECT_EQ(c.start, 300LL);
    EXPECT_EQ(c.length, 800LL);
    EXPECT_EQ(c.offset, 1200LL);
    ClipOps::trimEndTo(c, 500);
    EXPECT_EQ(c.length, 200LL);
    ClipOps::trimEndTo(c, -50);
    EXPECT_EQ(c.length, 1LL);
    Session::Clip c2 = audioClip(0, 0, 1000);
    c2.fadeOut = {800, Session::FadeShape::Linear, 0.0f};
    ClipOps::trimStartTo(c2, 900);
    EXPECT_TRUE(c2.fadeOut.len <= c2.length);
    return true;
}

static bool test_trims_saturate_on_extreme_arguments() {
    const long long maxPos = ClipOps::kMaxPosition;
    const long long ends[] = {std::numeric_limits<long long>::min(), -maxPos * 2, -1, 0, maxPos, maxPos * 2,
                              std::numeric_limits<long long>::max()};
    for (long long newStart : ends) {
        Session::Clip c = audioClip(1000, maxPos / 2, 1000);
        ClipOps::trimStartTo(c, newStart);
        EXPECT_TRUE(c.start >= -maxPos && c.start <= maxPos);
        EXPECT_TRUE(c.length >= 1 && c.length <= maxPos);
        EXPECT_EQ(ClipOps::end(c), maxPos / 2 + 1000);
    }
    for (long long newEnd : ends) {
        Session::Clip c = audioClip(1000, -(maxPos / 2), 1000);
        ClipOps::trimEndTo(c, newEnd);
        EXPECT_TRUE(c.length >= 1 && c.length <= maxPos);
        EXPECT_EQ(ClipOps::end(c) - c.start, c.length);
    }
    return true;
}

static bool test_trim_start_of_stretched_audio_advances_offset_by_source_time() {
    Session::Clip c1 = audioClip(0, 0, 2000);
    c1.stretch = 2.0;
    ClipOps::trimStartTo(c1, 1000);
    EXPECT_EQ(c1.offset, 500LL);
    Session::Clip c2 = audioClip(0, 0, 2000);
    c2.stretch = 0.5;
    ClipOps::trimStartTo(c2, 1000);
    EXPECT_EQ(c2.offset, 2000LL);
    Session::Clip c3 = audioClip(0, 0, 2000);
    ClipOps::trimStartTo(c3, 1000);
    EXPECT_EQ(c3.offset, 1000LL);
    return true;
}

void registerClipModelTests() {
    Test::add("fade_shapes_hit_endpoints", test_fade_shapes_hit_endpoints);
    Test::add("fade_curvature_is_bounded_monotonic_and_inverts_at_the_midpoint", test_fade_curvature_is_bounded_monotonic_and_inverts_at_the_midpoint);
    Test::add("fade_curvature_loads_legacy_data_and_sanitizes_hostile_values", test_fade_curvature_loads_legacy_data_and_sanitizes_hostile_values);
    Test::add("temporary_fades_keep_clip_gain_and_envelope_without_mutating_the_clip", test_temporary_fades_keep_clip_gain_and_envelope_without_mutating_the_clip);
    Test::add("gain_with_fades", test_gain_with_fades);
    Test::add("gain_envelope_interpolates", test_gain_envelope_interpolates);
    Test::add("trim_start_keeps_gain_envelope_at_cut", test_trim_start_keeps_gain_envelope_at_cut);
    Test::add("trims_keep_material_anchored", test_trims_keep_material_anchored);
    Test::add("trims_saturate_on_extreme_arguments", test_trims_saturate_on_extreme_arguments);
    Test::add("trim_start_of_stretched_audio_advances_offset_by_source_time", test_trim_start_of_stretched_audio_advances_offset_by_source_time);
}