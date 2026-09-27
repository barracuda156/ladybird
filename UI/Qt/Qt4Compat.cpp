/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Assertions.h>
#include <AK/Format.h>
#include <AK/StdLibExtras.h>
#include <LibGfx/Bitmap.h>
#include <UI/Qt/Qt4Compat.h>

#include <QByteArray>
#include <QList>
#include <QMetaMethod>
#include <QMetaObject>

namespace Ladybird {

namespace Detail {

FunctorSlotBase::FunctorSlotBase(QObject* context, int argument_count)
    : QObject(context)
    , m_argument_count(argument_count)
{
}

int FunctorSlotBase::qt_metacall(QMetaObject::Call call, int id, void** arguments)
{
    id = QObject::qt_metacall(call, id, arguments);
    if (id < 0)
        return id;

    if (call == QMetaObject::InvokeMetaMethod) {
        if (id == 0)
            invoke(arguments);
        --id;
    }
    return id;
}

void FunctorSlotBase::connect_to(QObject* sender, char const* signal)
{
    VERIFY(sender);

    // SIGNAL() prefixes the signature with the code '2'.
    VERIFY(signal && signal[0] == '2');
    auto signature = QMetaObject::normalizedSignature(signal + 1);

    auto const* meta_object = sender->metaObject();
    auto signal_index = meta_object->indexOfSignal(signature.constData());
    if (signal_index < 0) {
        dbgln("Qt4Compat: {} has no signal {}", meta_object->className(), signature.constData());
        VERIFY_NOT_REACHED();
    }

    auto parameter_count = meta_object->method(signal_index).parameterTypes().size();
    if (parameter_count < m_argument_count) {
        dbgln("Qt4Compat: signal {}::{} has {} arguments, the function takes {}", meta_object->className(), signature.constData(), parameter_count, m_argument_count);
        VERIFY_NOT_REACHED();
    }

    auto connected = QMetaObject::connect(sender, signal_index, this, QObject::staticMetaObject.methodCount());
    VERIFY(connected);
}

}

QImage qimage_from_bitmap(Gfx::Bitmap const& bitmap, Optional<Gfx::IntSize> size, bool ignore_alpha)
{
    auto width = bitmap.width();
    auto height = bitmap.height();
    if (size.has_value()) {
        width = min(width, size->width());
        height = min(height, size->height());
    }
    if (width <= 0 || height <= 0)
        return {};

    auto bitmap_format = bitmap.format();
    auto has_alpha = bitmap_format == Gfx::BitmapFormat::BGRA8888 || bitmap_format == Gfx::BitmapFormat::RGBA8888;
    auto is_bgr = bitmap_format == Gfx::BitmapFormat::BGRx8888 || bitmap_format == Gfx::BitmapFormat::BGRA8888;
    VERIFY(is_bgr || bitmap_format == Gfx::BitmapFormat::RGBx8888 || bitmap_format == Gfx::BitmapFormat::RGBA8888);

    auto use_alpha = has_alpha && !ignore_alpha;
    auto format = QImage::Format_RGB32;
    if (use_alpha)
        format = bitmap.alpha_type() == Gfx::AlphaType::Premultiplied ? QImage::Format_ARGB32_Premultiplied : QImage::Format_ARGB32;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (is_bgr)
        return QImage(bitmap.scanline_u8(0), width, height, static_cast<int>(bitmap.pitch()), format);
#endif

    QImage image(width, height, format);
    if (image.isNull())
        return {};

    // Build every word from the bytes, which gives the same result on either byte order.
    auto red_offset = is_bgr ? 2 : 0;
    auto blue_offset = is_bgr ? 0 : 2;
    for (int y = 0; y < height; ++y) {
        auto const* source = bitmap.scanline_u8(y);
        auto* destination = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x, source += 4) {
            u32 alpha = use_alpha ? source[3] : 0xff;
            destination[x] = (alpha << 24) | (static_cast<u32>(source[red_offset]) << 16) | (static_cast<u32>(source[1]) << 8) | source[blue_offset];
        }
    }
    return image;
}

}
