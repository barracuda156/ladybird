/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Error.h>
#include <AK/Span.h>
#include <AK/Vector.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/DataView.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/WebGL/HostByteOrder.h>
#include <LibWeb/WebIDL/Buffers.h>

namespace Web::WebGL {

// What a page hands to WebGL in typed arrays, as OpenGL takes it (see HostByteOrder.h). Where the byte order of
// the host is the one of typed arrays, these classes are the memory of the typed array and nothing more.

// Numbers: the values of uniforms and of vertex attributes.
template<typename T>
class HostOrderNumbers {
public:
    HostOrderNumbers(Span<T> numbers, bool are_in_the_order_of_typed_arrays)
        : m_numbers(numbers)
    {
        if constexpr (page_and_host_byte_order_differ && sizeof(T) > 1) {
            if (are_in_the_order_of_typed_arrays && !numbers.is_empty()) {
                m_reversed.resize(numbers.size());
                __builtin_memcpy(m_reversed.data(), numbers.data(), numbers.size() * sizeof(T));
                reverse_elements(reinterpret_cast<u8*>(m_reversed.data()), numbers.size() * sizeof(T), sizeof(T));
            }
        } else {
            (void)are_in_the_order_of_typed_arrays;
        }
    }

    T const* data() const { return m_reversed.is_empty() ? m_numbers.data() : m_reversed.data(); }
    size_t size() const { return m_numbers.size(); }

private:
    Span<T> m_numbers;
    Vector<T> m_reversed;
};

// Bytes that are numbers of one size: pixels.
class HostOrderBytes {
public:
    static ErrorOr<HostOrderBytes> create(ReadonlyBytes bytes, size_t element_size)
    {
        HostOrderBytes host_order_bytes { bytes };
        if constexpr (page_and_host_byte_order_differ) {
            if (element_size > 1 && !bytes.is_empty()) {
                host_order_bytes.m_reversed = TRY(ByteBuffer::copy(bytes));
                reverse_elements(host_order_bytes.m_reversed.data(), host_order_bytes.m_reversed.size(), element_size);
                host_order_bytes.m_is_reversed = true;
            }
        } else {
            (void)element_size;
        }
        return host_order_bytes;
    }

    u8 const* data() const { return m_is_reversed ? m_reversed.data() : m_bytes.data(); }
    size_t size() const { return m_bytes.size(); }

private:
    explicit HostOrderBytes(ReadonlyBytes bytes)
        : m_bytes(bytes)
    {
    }

    ReadonlyBytes m_bytes;
    ByteBuffer m_reversed;
    bool m_is_reversed { false };
};

// Numbers that OpenGL has written, where a typed array is made of them or has them already.
inline void bring_to_the_order_of_typed_arrays(Bytes bytes, size_t element_size)
{
    if constexpr (page_and_host_byte_order_differ)
        reverse_elements(bytes.data(), bytes.size(), element_size);
    else
        (void)bytes, (void)element_size;
}

// The size of the elements of the typed array that a buffer source is, and 1 for an ArrayBuffer and a DataView,
// which have no elements.
inline size_t element_size_of(WebIDL::BufferableObjectBase const& source)
{
    if (auto const* typed_array = as_if<JS::TypedArrayBase>(*source.raw_object()))
        return typed_array->element_size();
    return 1;
}

}
