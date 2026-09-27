/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// What the Qt 4 build of this frontend needs beyond Qt 4 itself: connecting signals to lambdas
// (Qt 4 connects a signal only to a slot named by a string), and handing Gfx::Bitmap pixels to a
// QImage (Qt 4 has only host-order 32-bit formats).

#include <AK/Optional.h>
#include <AK/Types.h>
#include <LibGfx/Forward.h>
#include <LibGfx/Size.h>

#include <QImage>
#include <QObject>
#include <QTimer>

#include <type_traits>
#include <utility>

namespace Ladybird {

namespace Detail {

// Receives one signal and calls a function with its arguments. Qt 4 can connect a signal to any
// method index of a receiver (QMetaObject::connect) and delivers the call to the receiver's
// qt_metacall(). A subclass that moc has not seen has only QObject's methods, so the first index
// after them is free for this; no moc is involved.
class FunctorSlotBase : public QObject {
public:
    FunctorSlotBase(QObject* context, int argument_count);

    virtual int qt_metacall(QMetaObject::Call, int id, void** arguments) override;

    void connect_to(QObject* sender, char const* signal);

protected:
    virtual void invoke(void** arguments) = 0;

private:
    int m_argument_count { 0 };
};

template<typename Callable, typename... Args>
class FunctorSlot final : public FunctorSlotBase {
public:
    FunctorSlot(QObject* context, Callable callable)
        : FunctorSlotBase(context, static_cast<int>(sizeof...(Args)))
        , m_callable(std::move(callable))
    {
    }

private:
    virtual void invoke(void** arguments) override
    {
        invoke_with_arguments(arguments, std::index_sequence_for<Args...> {});
    }

    template<size_t... Indices>
    void invoke_with_arguments([[maybe_unused]] void** arguments, std::index_sequence<Indices...>)
    {
        // arguments[0] is the return value, the signal's arguments follow.
        m_callable(*reinterpret_cast<std::remove_cvref_t<Args>*>(arguments[Indices + 1])...);
    }

    Callable m_callable;
};

template<typename Callable, typename Return, typename Class, typename... Args>
FunctorSlotBase* make_functor_slot(QObject* context, Callable&& callable, Return (Class::*)(Args...) const)
{
    return new FunctorSlot<std::decay_t<Callable>, Args...>(context, std::forward<Callable>(callable));
}

template<typename Callable, typename Return, typename Class, typename... Args>
FunctorSlotBase* make_functor_slot(QObject* context, Callable&& callable, Return (Class::*)(Args...))
{
    return new FunctorSlot<std::decay_t<Callable>, Args...>(context, std::forward<Callable>(callable));
}

}

// Connects a signal, named with SIGNAL(), to a lambda. The lambda may take fewer arguments than
// the signal, and its argument types have to be the signal's. The connection ends when the
// context is destroyed. A signal that the sender does not have is a programming error: this
// aborts, where QObject::connect() would only print a warning and leave nothing connected.
template<typename Callable>
void connect(QObject* sender, char const* signal, QObject* context, Callable&& callable)
{
    auto* slot = Detail::make_functor_slot(context, std::forward<Callable>(callable), &std::remove_cvref_t<Callable>::operator());
    slot->connect_to(sender, signal);
}

// Calls a lambda once after the given time, unless the context is destroyed first.
template<typename Callable>
void single_shot(int milliseconds, QObject* context, Callable&& callable)
{
    auto* timer = new QTimer(context);
    timer->setSingleShot(true);
    connect(timer, SIGNAL(timeout()), timer, [timer, callable = std::forward<Callable>(callable)]() mutable {
        timer->deleteLater();
        callable();
    });
    timer->start(milliseconds);
}

// The given part (by default all) of a bitmap as a QImage: QImage::Format_RGB32 if the bitmap has
// no alpha channel or it is to be ignored, otherwise Format_ARGB32 or Format_ARGB32_Premultiplied
// after the bitmap's alpha type. Those formats are 0xAARRGGBB words in host order, so the pixels
// are copied, except for a BGRx/BGRA bitmap on a little-endian host, whose memory already has that
// layout: the image then refers to the bitmap's memory and must not outlive it.
QImage qimage_from_bitmap(Gfx::Bitmap const&, Optional<Gfx::IntSize> size = {}, bool ignore_alpha = false);

}
