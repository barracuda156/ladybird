/*
 * Copyright (c) 2023, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Checked.h>
#include <LibGC/Heap.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibJS/Runtime/ExternalMemory.h>
#include <LibJS/Runtime/Realm.h>
#include <LibWeb/HTML/BitmapDecodedImageData.h>
#include <LibWeb/Painting/DisplayListRecorder.h>
#include <LibWeb/Painting/DisplayListRecordingContext.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(BitmapDecodedImageData);

ErrorOr<GC::Ref<BitmapDecodedImageData>> BitmapDecodedImageData::create(JS::Realm& realm, Vector<Frame>&& frames, size_t loop_count, bool animated)
{
    return realm.create<BitmapDecodedImageData>(move(frames), loop_count, animated);
}

BitmapDecodedImageData::BitmapDecodedImageData(Vector<Frame>&& frames, size_t loop_count, bool animated)
    : m_frames(move(frames))
    , m_loop_count(loop_count)
    , m_animated(animated)
{
}

BitmapDecodedImageData::~BitmapDecodedImageData() = default;

static size_t immutable_bitmap_external_memory_size(Gfx::ImmutableBitmap const& bitmap)
{
    if (auto backing_bitmap = bitmap.bitmap())
        return backing_bitmap->data_size();

    // NB: A bitmap that is backed by YUV planes has no Gfx::Bitmap. Count what it takes once it is converted.
    Checked<size_t> size = static_cast<size_t>(bitmap.width());
    size *= static_cast<size_t>(bitmap.height());
    size *= sizeof(u32);
    if (size.has_overflow())
        return NumericLimits<size_t>::max();
    return size.value();
}

size_t BitmapDecodedImageData::external_memory_size() const
{
    size_t size = JS::vector_external_memory_size(m_frames);
    for (auto const& frame : m_frames) {
        if (frame.bitmap)
            size = JS::saturating_add_external_memory_size(size, immutable_bitmap_external_memory_size(*frame.bitmap));
    }
    return size;
}

RefPtr<Gfx::ImmutableBitmap> BitmapDecodedImageData::bitmap(size_t frame_index, Gfx::IntSize) const
{
    if (frame_index >= m_frames.size())
        return nullptr;
    return m_frames[frame_index].bitmap;
}

int BitmapDecodedImageData::frame_duration(size_t frame_index) const
{
    if (frame_index >= m_frames.size())
        return 0;
    return m_frames[frame_index].duration;
}

Optional<CSSPixels> BitmapDecodedImageData::intrinsic_width() const
{
    return m_frames.first().bitmap->width();
}

Optional<CSSPixels> BitmapDecodedImageData::intrinsic_height() const
{
    return m_frames.first().bitmap->height();
}

Optional<CSSPixelFraction> BitmapDecodedImageData::intrinsic_aspect_ratio() const
{
    return CSSPixels(m_frames.first().bitmap->width()) / CSSPixels(m_frames.first().bitmap->height());
}

Optional<Gfx::IntRect> BitmapDecodedImageData::frame_rect(size_t frame_index) const
{
    return m_frames[frame_index].bitmap->rect();
}

void BitmapDecodedImageData::paint(DisplayListRecordingContext& context, size_t frame_index, Gfx::IntRect dst_rect, Gfx::IntRect clip_rect, Gfx::ScalingMode scaling_mode) const
{
    context.display_list_recorder().draw_scaled_immutable_bitmap(dst_rect, clip_rect, *m_frames[frame_index].bitmap, scaling_mode);
}

}
