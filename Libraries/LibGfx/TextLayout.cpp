/*
 * Copyright (c) 2018-2020, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021, sin-ack <sin-ack@protonmail.com>
 * Copyright (c) 2024-2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TextLayout.h"
#include <AK/TypeCasts.h>
#include <LibGfx/Point.h>
#include <harfbuzz/hb.h>

namespace Gfx {

Vector<NonnullRefPtr<GlyphRun>> shape_text(FloatPoint baseline_start, Utf8View string, FontCascadeList const& font_cascade_list)
{
    if (string.length() == 0)
        return {};

    Vector<NonnullRefPtr<GlyphRun>> runs;

    auto it = string.begin();
    auto substring_begin_offset = string.iterator_offset(it);
    Font const* last_font = &font_cascade_list.font_for_code_point(*it);
    FloatPoint last_position = baseline_start;

    auto add_run = [&runs, &last_position](Utf8View string, Font const& font) {
        auto run = shape_text(last_position, 0, string, font, GlyphRun::TextType::Common, {});
        last_position.translate_by(run->width(), 0);
        runs.append(*run);
    };

    while (it != string.end()) {
        auto code_point = *it;
        auto const* font = &font_cascade_list.font_for_code_point(code_point);
        if (font != last_font) {
            auto substring = string.substring_view(substring_begin_offset, string.iterator_offset(it) - substring_begin_offset);
            add_run(substring, *last_font);
            last_font = font;
            substring_begin_offset = string.iterator_offset(it);
        }
        ++it;
    }

    auto end_offset = string.iterator_offset(it);
    if (substring_begin_offset < end_offset) {
        auto substring = string.substring_view(substring_begin_offset, end_offset - substring_begin_offset);
        add_run(substring, *last_font);
    }

    return runs;
}

RefPtr<GlyphRun> shape_text(FloatPoint baseline_start, float letter_spacing, Utf8View string, Gfx::Font const& font, GlyphRun::TextType text_type, ShapeFeatures const& features)
{
    // Use thread-local buffer to avoid static contention and improve performance
    thread_local hb_buffer_t* buffer = nullptr;
    if (!buffer) {
        buffer = hb_buffer_create();
    }
    
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf8(buffer, reinterpret_cast<char const*>(string.bytes()), string.byte_length(), 0, -1);
    hb_buffer_guess_segment_properties(buffer);

    u32 glyph_count;
    auto* glyph_info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    Vector<hb_glyph_info_t> const input_glyph_info({ glyph_info, glyph_count });

    if (input_glyph_info.size() == 0) {
        hb_buffer_clear_contents(buffer);
        return nullptr;
    }

    auto* hb_font = font.harfbuzz_font();
    hb_feature_t const* hb_features_data = nullptr;
    Vector<hb_feature_t> hb_features;
    if (!features.is_empty()) {
        hb_features.ensure_capacity(features.size());
        for (auto const& feature : features) {
            hb_features.append({
                .tag = HB_TAG(feature.tag[0], feature.tag[1], feature.tag[2], feature.tag[3]),
                .value = feature.value,
                .start = 0,
                .end = HB_FEATURE_GLOBAL_END,
            });
        }
        hb_features_data = hb_features.data();
    }

    hb_shape(hb_font, buffer, hb_features_data, features.size());

    glyph_info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    auto* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);

    Vector<Gfx::DrawGlyph> glyph_run;
    FloatPoint point = baseline_start;
    
    for (size_t i = 0; i < glyph_count; ++i) {
        // Handle coordinate system conversion properly
        // HarfBuzz positions are relative to the baseline and need proper adjustment
        auto position = point
            + FloatPoint {
                positions[i].x_offset / text_shaping_resolution, 
                -positions[i].y_offset / text_shaping_resolution 
            };

        glyph_run.append({ position, glyph_info[i].codepoint });
        point += FloatPoint {
            positions[i].x_advance / text_shaping_resolution, 
            positions[i].y_advance / text_shaping_resolution 
        };

        // don't apply spacing to last glyph
        // https://drafts.csswg.org/css-text/#example-7880704e
        if (i != (glyph_count - 1))
            point.translate_by(letter_spacing, 0);
    }

    auto run = adopt_ref(*new Gfx::GlyphRun(move(glyph_run), font, text_type, point.x() - baseline_start.x()));
    
    // Clear buffer contents for next use - more efficient than reset
    hb_buffer_clear_contents(buffer);
    return run;
}

float measure_text_width(Utf8View const& string, Gfx::Font const& font, ShapeFeatures const& features)
{
    auto glyph_run = shape_text({}, 0, string, font, GlyphRun::TextType::Common, features);
    return glyph_run->width();
}

}
