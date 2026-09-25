/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/BitCast.h>
#include <AK/Endian.h>
#include <AK/Types.h>

namespace JS {

// Typed array elements are stored little-endian on every host: that is the [[LittleEndian]] value GetValueFromBuffer
// and SetValueInBuffer default to, and what WebAssembly memory and web content expect. Native code that reads or
// writes elements through TypedArray::data() converts them with this; it swaps bytes on big-endian hosts only, and is
// its own inverse. Unlike AK's helpers it also handles the 2-byte f16.
template<typename T>
ALWAYS_INLINE T convert_between_host_and_typed_array_order(T value)
{
    if constexpr (AK::HostIsLittleEndian || sizeof(T) == 1)
        return value;
    else if constexpr (sizeof(T) == 2)
        return bit_cast<T>(__builtin_bswap16(bit_cast<u16>(value)));
    else if constexpr (sizeof(T) == 4)
        return bit_cast<T>(__builtin_bswap32(bit_cast<u32>(value)));
    else
        return bit_cast<T>(__builtin_bswap64(bit_cast<u64>(value)));
}

}
