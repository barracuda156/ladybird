/*
 * Copyright (c) 2024, Gregory Bertilson <zaggy1024@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <LibGfx/Forward.h>
#include <LibMedia/CodecID.h>
#include <LibMedia/Export.h>
#include <LibMedia/VideoDecoder.h>

#include "FFmpegForward.h"

namespace Media::FFmpeg {

class MEDIA_API FFmpegVideoDecoder final : public VideoDecoder {
public:
    static DecoderErrorOr<NonnullOwnPtr<FFmpegVideoDecoder>> try_create(CodecID, ReadonlyBytes codec_initialization_data);
    FFmpegVideoDecoder(AVCodecContext* codec_context, AVPacket* packet, AVFrame* frame);
    virtual ~FFmpegVideoDecoder() override;

    virtual DecoderErrorOr<void> receive_coded_data(AK::Duration timestamp, AK::Duration duration, ReadonlyBytes coded_data) override;
    virtual void signal_end_of_stream() override;
    virtual DecoderErrorOr<NonnullOwnPtr<VideoFrame>> get_decoded_frame(CodingIndependentCodePoints const& container_cicp) override;

    virtual void flush() override;

private:
    struct ScalingParameters {
        int width { 0 };
        int height { 0 };
        int source_format { -1 };
        int matrix { 0 };
        bool full_range { false };

        bool operator==(ScalingParameters const&) const = default;
    };

    DecoderErrorOr<NonnullRefPtr<Gfx::ImmutableBitmap>> convert_frame_to_rgb(CodingIndependentCodePoints const&);

    AVCodecContext* m_codec_context;
    AVPacket* m_packet;
    AVFrame* m_frame;

    SwsContext* m_scaling_context { nullptr };
    ScalingParameters m_scaling_parameters;
};

}
