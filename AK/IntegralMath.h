/*
 * Copyright (c) 2022, Leon Albrecht <leon2002.la@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Assertions.h>
#include <AK/BuiltinWrappers.h>
#include <AK/Concepts.h>
#include <AK/Platform.h>
#include <AK/Types.h>

namespace AK {

template<Integral T>
constexpr T exp2(T exponent)
{
    return static_cast<T>(1) << exponent;
}

template<Integral T>
constexpr T log2(T x)
{
    return x ? (8 * sizeof(T) - 1) - count_leading_zeroes(static_cast<MakeUnsigned<T>>(x)) : 0;
}

template<Integral T>
constexpr T ceil_log2(T x)
{
    if (x <= 1)
        return 0;

    return AK::log2(x - 1) + 1;
}

template<Integral I>
constexpr I pow(I base, I exponent)
{
    // https://en.wikipedia.org/wiki/Exponentiation_by_squaring
    if (exponent < 0)
        return 0;
    if (exponent == 0)
        return 1;

    I res = 1;
    while (exponent > 0) {
        if (exponent & 1)
            res *= base;
        base *= base;
        exponent /= 2u;
    }
    return res;
}

template<auto base, Unsigned U = decltype(base)>
constexpr bool is_power_of(U x)
{
    if constexpr (base == 1)
        return x == 1;
    else if constexpr (base == 2)
        return is_power_of_two(x);

    if (base == 0 && x == 0)
        return true;
    if (base == 0 || x == 0)
        return false;

    while (x != 1) {
        if (x % base != 0)
            return false;
        x /= base;
    }
    return true;
}

template<Unsigned T>
constexpr T reinterpret_as_octal(T decimal)
{
    T result = 0;
    T n = 0;
    while (decimal > 0) {
        result += pow<T>(8, n++) * (decimal % 10);
        decimal /= 10;
    }
    return result;
}

template<Unsigned T>
constexpr T gcd(T x, T y)
{
    if (x == 0)
        return y;
    if (y == 0)
        return x;

    int shift = 0;
    while (((x | y) & 1) == 0) {
        x >>= 1;
        y >>= 1;
        shift++;
    }

    while (x != y) {
        if (x & 1) {
            if (y & 1) {
                if (x > y)
                    x -= y;
                else
                    y -= x;
            } else {
                y >>= 1;
            }
        } else {
            x >>= 1;
            if (y & 1) {
                if (x < y)
                    swap(x, y);
            }
        }
    }

    return x << shift;
}

template<Signed T>
constexpr T gcd(T x, T y)
{
    return gcd(static_cast<MakeUnsigned<T>>(abs(x)), static_cast<MakeUnsigned<T>>(abs(y)));
}

template<Unsigned T>
constexpr T lcm(T x, T y)
{
    if (x == 0 || y == 0)
        return 0;
    return x / gcd(x, y) * y;
}

template<Signed T>
constexpr T lcm(T x, T y)
{
    return lcm(static_cast<MakeUnsigned<T>>(abs(x)), static_cast<MakeUnsigned<T>>(abs(y)));
}

#ifndef __SIZEOF_INT128__
namespace Detail {

struct WideProduct {
    u64 high { 0 };
    u64 low { 0 };
};

// The 128-bit product of two 64-bit numbers, from the four products of their 32-bit halves.
constexpr WideProduct wide_multiply(u64 multiplicand, u64 multiplier)
{
    u64 const multiplicand_low = multiplicand & 0xffff'ffff;
    u64 const multiplicand_high = multiplicand >> 32;
    u64 const multiplier_low = multiplier & 0xffff'ffff;
    u64 const multiplier_high = multiplier >> 32;

    u64 const low_low = multiplicand_low * multiplier_low;
    u64 const low_high = multiplicand_low * multiplier_high;
    u64 const high_low = multiplicand_high * multiplier_low;
    u64 const high_high = multiplicand_high * multiplier_high;

    // Three numbers below 2^32 each, so this cannot overflow.
    u64 const middle = (low_low >> 32) + (low_high & 0xffff'ffff) + (high_low & 0xffff'ffff);

    return {
        .high = high_high + (low_high >> 32) + (high_low >> 32) + (middle >> 32),
        .low = (middle << 32) | (low_low & 0xffff'ffff),
    };
}

// The quotient of a 128-bit dividend whose upper half is below the divisor, so that the quotient fits into 64 bits.
constexpr u64 wide_divide(WideProduct dividend, u64 divisor)
{
    // Restoring division, one bit of the quotient per step. The remainder is below the divisor before each step,
    // so it has up to 65 bits after the shift: the bit that leaves the u64 is kept in carry.
    u64 remainder = dividend.high;
    u64 quotient = 0;
    for (int bit = 63; bit >= 0; --bit) {
        bool const carry = (remainder >> 63) != 0;
        remainder = (remainder << 1) | ((dividend.low >> bit) & 1);
        quotient <<= 1;
        if (carry || remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1;
        }
    }
    return quotient;
}

}
#endif

constexpr bool multiply_divide_would_overflow(u64 multiplicand, u64 multiplier, u64 divisor)
{
    VERIFY(divisor != 0);
#ifdef __SIZEOF_INT128__
    auto product = static_cast<unsigned __int128>(multiplicand) * multiplier;
    return static_cast<u64>(product >> 64) >= divisor;
#else
    return Detail::wide_multiply(multiplicand, multiplier).high >= divisor;
#endif
}

constexpr u64 multiply_divide(u64 multiplicand, u64 multiplier, u64 divisor)
{
    VERIFY(!multiply_divide_would_overflow(multiplicand, multiplier, divisor));
#ifdef __SIZEOF_INT128__
    auto product = static_cast<unsigned __int128>(multiplicand) * multiplier;

#    if ARCH(X86_64)
    // x86-64 divides the whole product in one instruction, which faults unless the quotient fits in 64 bits.
    if !consteval {
        u64 quotient = 0;
        u64 remainder = 0;
        asm("divq %[divisor]"
            : "=a"(quotient), "=d"(remainder)
            : [divisor] "r"(divisor), "a"(static_cast<u64>(product)), "d"(static_cast<u64>(product >> 64)));
        return quotient;
    }
#    endif
    return static_cast<u64>(product / divisor);
#else
    return Detail::wide_divide(Detail::wide_multiply(multiplicand, multiplier), divisor);
#endif
}

}
