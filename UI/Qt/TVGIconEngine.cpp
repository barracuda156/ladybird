/*
 * Copyright (c) 2023, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/MemoryStream.h>
#include <AK/String.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Rect.h>
#include <UI/Qt/Qt4Compat.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/TVGIconEngine.h>

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmapCache>

namespace Ladybird {

void TVGIconEngine::paint(QPainter* qpainter, QRect const& rect, QIcon::Mode mode, QIcon::State state)
{
    qpainter->drawPixmap(rect, pixmap(rect.size(), mode, state));
}

QIconEngineV2* TVGIconEngine::clone() const
{
    return new TVGIconEngine(*this);
}

QPixmap TVGIconEngine::pixmap(QSize const& size, QIcon::Mode mode, QIcon::State state)
{
    QPixmap pixmap;
    auto key = pixmap_cache_key(size, mode, state);
    if (QPixmapCache::find(key, &pixmap))
        return pixmap;
    auto bitmap = MUST(m_image_data->bitmap({ size.width(), size.height() }));

    // Filter the QImage, not the bitmap: a QRgb is 0xAARRGGBB like a Color on either byte order, while the u32
    // pixels of a BGRA8888 bitmap are that only on little-endian hosts (Skia writes the bytes B, G, R, A). The
    // filters return colors that are not premultiplied, hence ARGB32.
    auto image = qimage_from_bitmap(*bitmap).convertToFormat(QImage::Format_ARGB32);

    for (auto const& filter : m_filters) {
        if (filter->mode() == mode) {
            for (int y = 0; y < image.height(); ++y) {
                auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    auto original_color = Color::from_bgra(pixels[x]);
                    auto filtered_color = filter->function()(original_color);
                    pixels[x] = filtered_color.value();
                }
            }
            break;
        }
    }

    pixmap = QPixmap::fromImage(image);
    if (!pixmap.isNull())
        QPixmapCache::insert(key, pixmap);
    return pixmap;
}

QString TVGIconEngine::pixmap_cache_key(QSize const& size, QIcon::Mode mode, QIcon::State state)
{
    return qstring_from_ak_string(
        MUST(String::formatted("$sernity_tvgicon_{}_{}x{}_{}_{}", m_cache_id, size.width(), size.height(), to_underlying(mode), to_underlying(state))));
}

void TVGIconEngine::add_filter(QIcon::Mode mode, Function<Color(Color)> filter)
{
    m_filters.empend(adopt_ref(*new Filter(mode, move(filter))));
    invalidate_cache();
}

TVGIconEngine* TVGIconEngine::from_file(QString const& path)
{
    QFile icon_resource(path);
    if (!icon_resource.open(QIODevice::ReadOnly))
        return nullptr;
    auto icon_data = icon_resource.readAll();
    FixedMemoryStream icon_bytes { ReadonlyBytes { icon_data.data(), static_cast<size_t>(icon_data.size()) } };
    if (auto tvg = Gfx::TinyVGDecodedImageData::decode(icon_bytes); !tvg.is_error())
        return new TVGIconEngine(tvg.release_value());
    return nullptr;
}

}
