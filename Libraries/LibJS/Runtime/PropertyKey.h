/*
 * Copyright (c) 2020, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/CharacterTypes.h>
#include <AK/Utf16FlyString.h>
#include <LibJS/Runtime/Completion.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/Symbol.h>

namespace JS {

class PropertyKey {
public:
    enum class StringMayBeNumber {
        Yes,
        No,
    };

    static ThrowCompletionOr<PropertyKey> from_value(VM& vm, Value value)
    {
        VERIFY(!value.is_special_empty_value());
        if (value.is_symbol())
            return PropertyKey { value.as_symbol() };
        if (value.is_integral_number() && value.as_double() >= 0 && value.as_double() < NumericLimits<u32>::max())
            return static_cast<u32>(value.as_double());
        return TRY(value.to_utf16_string(vm));
    }

    static constexpr uintptr_t NORMAL_STRING_FLAG = 0;
    static constexpr uintptr_t SHORT_STRING_FLAG = 1;
    static constexpr uintptr_t SYMBOL_FLAG = 2;
    static constexpr uintptr_t NUMBER_FLAG = 3;

    // The tag lives in the low bits of the word that m_bits aliases. On 64-bit hosts that word is the
    // whole 8-byte storage. Numeric keys need 34 bits (a u32 index past the two tag bits), so the
    // storage stays 8 bytes on 32-bit hosts too; there m_bits aliases only the first word of
    // m_number (the high word on big-endian hosts), which therefore carries the tag and the low 30
    // index bits while the other word holds the remaining two.
#if defined(AK_ARCH_64_BIT)
    static constexpr u64 number_storage(u32 index) { return static_cast<u64>(index) << 2 | NUMBER_FLAG; }
    static constexpr u32 number_from_storage(u64 storage) { return static_cast<u32>(storage >> 2); }
    static constexpr u64 pointer_storage(uintptr_t bits) { return bits; }
#else
    static constexpr u64 join_words(u32 first_word, u32 second_word)
    {
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return static_cast<u64>(first_word) << 32 | second_word;
#    else
        return static_cast<u64>(second_word) << 32 | first_word;
#    endif
    }
    static constexpr u32 first_word(u64 storage)
    {
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return static_cast<u32>(storage >> 32);
#    else
        return static_cast<u32>(storage);
#    endif
    }
    static constexpr u32 second_word(u64 storage)
    {
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return static_cast<u32>(storage);
#    else
        return static_cast<u32>(storage >> 32);
#    endif
    }
    static constexpr u64 number_storage(u32 index) { return join_words(index << 2 | NUMBER_FLAG, index >> 30); }
    static constexpr u32 number_from_storage(u64 storage) { return first_word(storage) >> 2 | second_word(storage) << 30; }
    static constexpr u64 pointer_storage(uintptr_t bits) { return join_words(bits, 0); }
#endif

    bool is_string() const { return (m_bits & 3) == NORMAL_STRING_FLAG || (m_bits & 3) == SHORT_STRING_FLAG; }
    bool is_number() const { return (m_bits & 3) == NUMBER_FLAG; }
    bool is_symbol() const { return (m_bits & 3) == SYMBOL_FLAG; }

    PropertyKey() = delete;

    PropertyKey(PropertyKey const& other)
    {
        if (other.is_string()) {
            m_number = 0;
            new (&m_string) Utf16FlyString(other.m_string);
        } else {
            m_number = other.m_number;
        }
    }

    PropertyKey(PropertyKey&& other) noexcept
    {
        if (other.is_string()) {
            m_number = 0;
            new (&m_string) Utf16FlyString(move(other.m_string));
        } else {
            m_number = exchange(other.m_number, 0);
        }
    }

    template<Integral T>
    PropertyKey(T index)
    {
        // FIXME: Replace this with requires(IsUnsigned<T>)?
        //        Needs changes in various places using `int` (but not actually being in the negative range)
        VERIFY(index >= 0);
        if constexpr (NumericLimits<T>::max() >= NumericLimits<u32>::max()) {
            if (index >= NumericLimits<u32>::max()) {
                m_number = 0;
                new (&m_string) Utf16FlyString { Utf16String::number(index) };
                return;
            }
        }
        m_number = number_storage(static_cast<u32>(index));
    }

    PropertyKey(Utf16FlyString string, StringMayBeNumber string_may_be_number = StringMayBeNumber::Yes)
    {
        if (string_may_be_number == StringMayBeNumber::Yes && !string.is_empty()) {
            auto first_code_unit = string.code_unit_at(0);
            if (is_ascii_digit(first_code_unit) && !(first_code_unit == '0' && string.length_in_code_units() > 1)) {
                auto property_index = string.to_number<u32>(TrimWhitespace::No);
                if (property_index.has_value() && property_index.value() < NumericLimits<u32>::max()) {
                    m_number = number_storage(property_index.release_value());
                    return;
                }
            }
        }

        m_number = 0;
        new (&m_string) Utf16FlyString(move(string));
    }

    PropertyKey(Utf16String const& string)
        : PropertyKey(Utf16FlyString { string })
    {
    }

    PropertyKey(GC::Ref<Symbol> symbol)
    {
        m_number = pointer_storage(reinterpret_cast<uintptr_t>(symbol.ptr()) | SYMBOL_FLAG);
    }

    PropertyKey& operator=(PropertyKey const& other)
    {
        if (this != &other) {
            if (is_string())
                m_string.~Utf16FlyString();
            new (this) PropertyKey(other);
        }
        return *this;
    }

    PropertyKey& operator=(PropertyKey&& other) noexcept
    {
        if (this != &other) {
            if (is_string())
                m_string.~Utf16FlyString();
            new (this) PropertyKey(move(other));
        }
        return *this;
    }

    ~PropertyKey()
    {
        if (is_string())
            m_string.~Utf16FlyString();
    }

    u32 as_number() const
    {
        VERIFY(is_number());
        return number_from_storage(m_number);
    }

    Utf16FlyString const& as_string() const
    {
        VERIFY(is_string());
        return m_string;
    }

    Symbol const* as_symbol() const
    {
        VERIFY(is_symbol());
        return reinterpret_cast<Symbol const*>(m_bits & ~static_cast<uintptr_t>(3));
    }

    Value to_value(VM& vm) const
    {
        if (is_string())
            return Value { PrimitiveString::create(vm, as_string()) };
        if (is_symbol())
            return Value { as_symbol() };
        return Value { PrimitiveString::create_from_unsigned_integer(vm, as_number()) };
    }

    Utf16String to_string() const
    {
        if (is_string())
            return as_string().to_utf16_string();
        if (is_symbol())
            return as_symbol()->descriptive_string();
        return Utf16String::number(as_number());
    }

    void visit_edges(Cell::Visitor& visitor) const
    {
        if (is_symbol())
            visitor.visit(const_cast<Symbol*>(as_symbol()));
    }

    bool operator==(PropertyKey const& other) const
    {
        if (is_string())
            return other.is_string() && m_string == other.m_string;
        if (is_symbol())
            return other.is_symbol() && as_symbol() == other.as_symbol();
        if (other.is_number())
            return as_number() == other.as_number();
        return false;
    }

private:
    friend Traits<PropertyKey>;
    friend class Optional<PropertyKey>;

    enum class ShouldMakeEmptyOptional {
        Indeed,
    };

    explicit PropertyKey(ShouldMakeEmptyOptional)
        : m_number(0)
    {
    }

    [[nodiscard]] bool is_empty_optional() const { return m_number == 0; }

    union {
        Utf16FlyString m_string;
        u64 m_number;
        Symbol const* m_symbol;
        uintptr_t m_bits;
    };
};

static_assert(sizeof(PropertyKey) == sizeof(u64));

}

namespace AK {

template<>
struct Traits<JS::PropertyKey> : public DefaultTraits<JS::PropertyKey> {
    static unsigned hash(JS::PropertyKey const& name)
    {
        if (name.is_string())
            return name.as_string().hash();
        if (name.is_symbol())
            return ptr_hash(name.as_symbol());
        if (name.is_number())
            return u32_hash(name.as_number());
        VERIFY_NOT_REACHED();
    }

    static bool equals(JS::PropertyKey const& a, JS::PropertyKey const& b)
    {
        if (a.is_string())
            return b.is_string() && a.as_string() == b.as_string();
        if (a.is_symbol())
            return b.is_symbol() && a.as_symbol() == b.as_symbol();
        if (a.is_number())
            return b.is_number() && a.as_number() == b.as_number();
        VERIFY_NOT_REACHED();
    }
};

template<>
struct Formatter<JS::PropertyKey> : Formatter<Utf16String> {
    ErrorOr<void> format(FormatBuilder& builder, JS::PropertyKey const& property_key)
    {
        if (property_key.is_number())
            return builder.put_u64(property_key.as_number());
        return Formatter<Utf16String>::format(builder, property_key.to_string());
    }
};

template<>
class Optional<JS::PropertyKey> : public OptionalBase<JS::PropertyKey> {
    template<typename U>
    friend class Optional;

public:
    using ValueType = JS::PropertyKey;

    Optional() = default;

    template<SameAs<OptionalNone> V>
    Optional(V) { }

    Optional(Optional<JS::PropertyKey> const& other)
    {
        if (other.has_value())
            m_value = other.m_value;
    }

    Optional(Optional&& other)
        : m_value(other.m_value)
    {
    }

    template<typename U = JS::PropertyKey>
    requires(!IsSame<OptionalNone, RemoveCVReference<U>>)
    explicit(!IsConvertible<U&&, JS::PropertyKey>) Optional(U&& value)
    requires(!IsSame<RemoveCVReference<U>, Optional<JS::PropertyKey>> && IsConstructible<JS::PropertyKey, U &&>)
        : m_value(forward<U>(value))
    {
    }

    template<SameAs<OptionalNone> V>
    Optional& operator=(V)
    {
        clear();
        return *this;
    }

    Optional& operator=(Optional const& other)
    {
        if (this != &other) {
            clear();
            m_value = other.m_value;
        }
        return *this;
    }

    Optional& operator=(Optional&& other)
    {
        if (this != &other) {
            clear();
            m_value = other.m_value;
        }
        return *this;
    }

    void clear()
    {
        m_value = JS::PropertyKey { JS::PropertyKey::ShouldMakeEmptyOptional::Indeed };
    }

    [[nodiscard]] bool has_value() const
    {
        return !m_value.is_empty_optional();
    }

    [[nodiscard]] JS::PropertyKey& value() &
    {
        VERIFY(has_value());
        return m_value;
    }

    [[nodiscard]] JS::PropertyKey const& value() const&
    {
        VERIFY(has_value());
        return m_value;
    }

    [[nodiscard]] JS::PropertyKey value() &&
    {
        return release_value();
    }

    [[nodiscard]] JS::PropertyKey release_value()
    {
        VERIFY(has_value());
        JS::PropertyKey released_value = m_value;
        clear();
        return released_value;
    }

private:
    JS::PropertyKey m_value { JS::PropertyKey::ShouldMakeEmptyOptional::Indeed };
};

}
