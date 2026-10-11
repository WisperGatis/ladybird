/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Vector.h>
#include <LibGfx/Bitmap.h>
#include <LibTest/TestCase.h>

TEST_CASE(create_wrapper_rejects_undersized_buffer)
{
    // The buffer is one byte short of what a 4x4 RGBA8888 bitmap (pitch 16, height 4) requires.
    Gfx::IntSize const size { 4, 4 };
    auto const pitch = Gfx::Bitmap::minimum_pitch(size.width(), Gfx::BitmapFormat::RGBA8888);
    auto const required_size = Gfx::Bitmap::size_in_bytes(pitch, size.height());

    Vector<u8> undersized_storage;
    undersized_storage.resize(required_size - 1);

    auto result = Gfx::Bitmap::create_wrapper(Gfx::BitmapFormat::RGBA8888, Gfx::AlphaType::Unpremultiplied, size, pitch, undersized_storage.data(), undersized_storage.size());
    EXPECT(result.is_error());
}

TEST_CASE(create_wrapper_accepts_correctly_sized_buffer)
{
    Gfx::IntSize const size { 4, 4 };
    auto const pitch = Gfx::Bitmap::minimum_pitch(size.width(), Gfx::BitmapFormat::RGBA8888);
    auto const required_size = Gfx::Bitmap::size_in_bytes(pitch, size.height());

    Vector<u8> storage;
    storage.resize(required_size);

    auto result = Gfx::Bitmap::create_wrapper(Gfx::BitmapFormat::RGBA8888, Gfx::AlphaType::Unpremultiplied, size, pitch, storage.data(), storage.size());
    EXPECT(!result.is_error());
}

TEST_CASE(create_wrapper_accepts_oversized_buffer)
{
    // A buffer larger than strictly required (e.g. one with row padding beyond the claimed pitch)
    // must still be accepted; the check is a lower bound, not an exact-match requirement.
    Gfx::IntSize const size { 4, 4 };
    auto const pitch = Gfx::Bitmap::minimum_pitch(size.width(), Gfx::BitmapFormat::RGBA8888);
    auto const required_size = Gfx::Bitmap::size_in_bytes(pitch, size.height());

    Vector<u8> storage;
    storage.resize(required_size + 64);

    auto result = Gfx::Bitmap::create_wrapper(Gfx::BitmapFormat::RGBA8888, Gfx::AlphaType::Unpremultiplied, size, pitch, storage.data(), storage.size());
    EXPECT(!result.is_error());
}
