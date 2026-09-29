/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Endian.h>
#include <AK/Types.h>

namespace Web::WebGL {

// Typed arrays keep their elements little-endian on every host (see LibJS/Runtime/TypedArrayElementOrder.h), so the
// memory that a page hands to WebGL is laid out as on a little-endian machine, whatever views the page wrote it
// through. OpenGL takes numbers in the byte order of the host. Where the two differ, the numbers change their byte
// order on the way: uniforms and pixels by the type that the call names, the contents of buffers by the vertex
// attributes and index types that read them when something is drawn.
//
// This header has the parts of that which need nothing but bytes. On a little-endian host none of it is used.
inline constexpr bool page_and_host_byte_order_differ = !AK::HostIsLittleEndian;

// Reverses the bytes of every whole element. It is its own inverse.
constexpr void reverse_elements(u8* bytes, size_t byte_count, size_t element_size)
{
    if (element_size < 2)
        return;
    for (size_t start = 0; start + element_size <= byte_count; start += element_size) {
        for (size_t low = start, high = start + element_size - 1; low < high; ++low, --high) {
            auto byte = bytes[low];
            bytes[low] = bytes[high];
            bytes[high] = byte;
        }
    }
}

// The size of the numbers that pixels of this OpenGL type are made of: 1 for bytes and for what is not known.
constexpr size_t element_size_of_pixel_type(u32 type)
{
    switch (type) {
    case 0x1402: // GL_SHORT
    case 0x1403: // GL_UNSIGNED_SHORT
    case 0x140B: // GL_HALF_FLOAT
    case 0x8D61: // GL_HALF_FLOAT_OES
    case 0x8033: // GL_UNSIGNED_SHORT_4_4_4_4
    case 0x8034: // GL_UNSIGNED_SHORT_5_5_5_1
    case 0x8363: // GL_UNSIGNED_SHORT_5_6_5
        return 2;
    case 0x1404: // GL_INT
    case 0x1405: // GL_UNSIGNED_INT
    case 0x1406: // GL_FLOAT
    case 0x84FA: // GL_UNSIGNED_INT_24_8
    case 0x8368: // GL_UNSIGNED_INT_2_10_10_10_REV
    case 0x8C3B: // GL_UNSIGNED_INT_10F_11F_11F_REV
    case 0x8C3E: // GL_UNSIGNED_INT_5_9_9_9_REV
    case 0x8DAD: // GL_FLOAT_32_UNSIGNED_INT_24_8_REV: a float and an integer of 4 bytes each
        return 4;
    default:
        return 1;
    }
}

// The same for the type of a vertex attribute or of the indices.
constexpr size_t element_size_of_vertex_type(u32 type)
{
    switch (type) {
    case 0x1402: // GL_SHORT
    case 0x1403: // GL_UNSIGNED_SHORT
    case 0x140B: // GL_HALF_FLOAT
    case 0x8D61: // GL_HALF_FLOAT_OES
        return 2;
    case 0x1404: // GL_INT
    case 0x1405: // GL_UNSIGNED_INT
    case 0x1406: // GL_FLOAT
    case 0x140C: // GL_FIXED
    case 0x8368: // GL_UNSIGNED_INT_2_10_10_10_REV
    case 0x8D9F: // GL_INT_2_10_10_10_REV
        return 4;
    default:
        return 1;
    }
}

// Whether all components of a vertex of this type are in one number.
constexpr bool is_packed_vertex_type(u32 type)
{
    return type == 0x8368 || type == 0x8D9F;
}

// The numbers that one vertex attribute, or the indices, read from a buffer: element_count numbers of element_size
// bytes at offset, and again every stride bytes as long as a whole number lies before end. With element_size 1 these
// are bytes, which OpenGL must get as the page wrote them.
struct ElementRun {
    static constexpr u32 to_the_end_of_the_buffer = 0xffffffff;

    u32 offset { 0 };
    u32 end { to_the_end_of_the_buffer };
    u32 stride { 1 };
    u8 element_size { 1 };
    u8 element_count { 1 };

    constexpr bool operator==(ElementRun const&) const = default;

    constexpr u32 bytes_per_vertex() const { return static_cast<u32>(element_size) * element_count; }

    // Whether the byte belongs to a number of the run, and where that number starts.
    constexpr bool has_byte(u32 byte, u32& element_start) const
    {
        if (byte < offset || byte >= end)
            return false;
        auto within_vertex = (byte - offset) % stride;
        if (within_vertex >= bytes_per_vertex())
            return false;
        element_start = byte - within_vertex % element_size;
        // The subtraction cannot wrap: element_start < end here.
        return end - element_start >= element_size;
    }
};

struct RunRelation {
    // A byte belongs to numbers of both runs that differ in where they start or in their size.
    bool clash { false };
    // A byte belongs to the same number in both runs.
    bool share { false };
    // Every byte of the first run, as far as both runs reach, belongs to the same number in the second one.
    bool first_within_second { true };
};

constexpr u32 greatest_common_divisor(u32 a, u32 b)
{
    while (b != 0) {
        auto remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

// How two runs lie to each other between the first byte that both can have and the end of the shorter one, in a
// buffer of buffer_size bytes. Both repeat, so one common period says it all; when that is longer than
// longest_period, the runs are taken to clash, which costs a conversion and not the right picture.
constexpr RunRelation relation_of(ElementRun const& first, ElementRun const& second, u32 buffer_size, u32 longest_period = 4096)
{
    RunRelation relation;

    auto begin = first.offset > second.offset ? first.offset : second.offset;
    auto end = first.end < second.end ? first.end : second.end;
    if (end > buffer_size)
        end = buffer_size;
    if (begin >= end) {
        // The first run has nothing where both reach, so nothing of it is missing in the second one.
        return relation;
    }

    u64 period = static_cast<u64>(first.stride) / greatest_common_divisor(first.stride, second.stride) * second.stride;
    if (period > longest_period) {
        relation.clash = true;
        relation.first_within_second = false;
        return relation;
    }
    // Numbers that cross the end of a run do not belong to it, so the last period is looked at as well.
    auto length = end - begin;
    auto scan_end = length > 2 * period ? begin + static_cast<u32>(period) : end;
    auto tail_begin = length > 2 * period ? end - static_cast<u32>(period) : end;

    auto look_at = [&](u32 byte) {
        u32 start_in_first = 0;
        u32 start_in_second = 0;
        auto in_first = first.has_byte(byte, start_in_first);
        auto in_second = second.has_byte(byte, start_in_second);
        if (in_first && in_second) {
            if (first.element_size == second.element_size && start_in_first == start_in_second)
                relation.share = true;
            else
                relation.clash = true;
        }
        if (in_first && (!in_second || first.element_size != second.element_size || start_in_first != start_in_second))
            relation.first_within_second = false;
    };

    for (auto byte = begin; byte < scan_end; ++byte)
        look_at(byte);
    for (auto byte = tail_begin; byte < end; ++byte)
        look_at(byte);

    return relation;
}

// Whether the numbers of the wanted run are, all of them, numbers of the run that the bytes were converted with.
constexpr bool run_is_covered_by(ElementRun const& wanted, ElementRun const& converted, u32 buffer_size)
{
    if (wanted.element_size < 2 || wanted.element_size != converted.element_size)
        return false;
    if (converted.offset > wanted.offset)
        return false;
    auto wanted_end = wanted.end < buffer_size ? wanted.end : buffer_size;
    auto converted_end = converted.end < buffer_size ? converted.end : buffer_size;
    if (converted_end < wanted_end)
        return false;
    auto relation = relation_of(wanted, converted, buffer_size);
    return !relation.clash && relation.first_within_second;
}

// Reverses the bytes of the numbers of a run that lie in [range_begin, range_end) of the buffer. bytes points to
// the byte range_begin of the buffer; the range must not cut a number of the run in two (see widen_to_whole_elements).
constexpr void reverse_elements_of_run(u8* bytes, u32 range_begin, u32 range_end, ElementRun const& run, u32 buffer_size)
{
    if (run.element_size < 2)
        return;
    auto run_end = run.end < buffer_size ? run.end : buffer_size;
    if (range_end > run_end)
        range_end = run_end;
    if (range_begin >= range_end || run.offset >= range_end)
        return;

    // The first vertex with a number at or after range_begin.
    u32 vertex = 0;
    if (range_begin > run.offset) {
        vertex = (range_begin - run.offset) / run.stride;
        // Numbers of this vertex may lie before range_begin; they are skipped below.
    }
    for (u64 vertex_start = static_cast<u64>(run.offset) + static_cast<u64>(vertex) * run.stride; vertex_start < range_end; vertex_start += run.stride) {
        for (u32 element = 0; element < run.element_count; ++element) {
            u64 start = vertex_start + static_cast<u64>(element) * run.element_size;
            if (start < range_begin)
                continue;
            if (start + run.element_size > range_end)
                break;
            reverse_elements(bytes + (start - range_begin), run.element_size, run.element_size);
        }
    }
}

// Widens [range_begin, range_end) so that it holds every number of the run whole that it has a byte of.
constexpr void widen_to_whole_elements(u32& range_begin, u32& range_end, ElementRun const& run, u32 buffer_size)
{
    if (run.element_size < 2 || range_begin >= range_end)
        return;
    u32 element_start = 0;
    if (run.has_byte(range_begin, element_start) && element_start + run.element_size <= buffer_size)
        range_begin = element_start;
    if (run.has_byte(range_end - 1, element_start) && element_start + run.element_size <= buffer_size)
        range_end = element_start + run.element_size;
}

// A list with room for a fixed number of items, which the compiler can work with when it checks the functions below.
template<typename T, size_t Capacity>
struct FixedList {
    T items[Capacity] {};
    size_t count { 0 };

    constexpr size_t size() const { return count; }
    constexpr bool is_empty() const { return count == 0; }
    constexpr bool is_full() const { return count == Capacity; }
    constexpr T& operator[](size_t index) { return items[index]; }
    constexpr T const& operator[](size_t index) const { return items[index]; }
    constexpr T* begin() { return items; }
    constexpr T* end() { return items + count; }
    constexpr T const* begin() const { return items; }
    constexpr T const* end() const { return items + count; }

    constexpr void clear() { count = 0; }
    constexpr bool append(T const& item)
    {
        if (is_full())
            return false;
        items[count++] = item;
        return true;
    }
    constexpr void remove(size_t index)
    {
        for (auto i = index; i + 1 < count; ++i)
            items[i] = items[i + 1];
        --count;
    }
    constexpr bool contains(T const& item) const
    {
        for (auto const& candidate : *this) {
            if (candidate == item)
                return true;
        }
        return false;
    }
};

// A run that is known of a buffer. With numbers of 2 or 4 bytes, OpenGL has them in the byte order of the host up to
// run.end; draw calls have read them up to confirmed_end, the rest is taken to go on like that until a draw call
// says otherwise. With bytes, the run says where a draw call has read bytes, which numbers must keep away from.
struct ConvertedRun {
    ElementRun run;
    u32 confirmed_end { 0 };
    // It comes from the elements of a typed array and not from a draw call.
    bool guessed { false };

    constexpr bool operator==(ConvertedRun const&) const = default;
    constexpr bool has_numbers() const { return run.element_size >= 2; }
};

// No two of them have a byte of a number in common.
using ConvertedRuns = FixedList<ConvertedRun, 32>;
// What one draw call reads from one buffer: a run for every vertex attribute, or one for the indices. They start
// at the first vertex that is read and end behind the last one.
using WantedRuns = FixedList<ElementRun, 65>;

// Reverses the numbers of all the runs in the bytes [range_begin, range_end) of the buffer, which bytes points to.
// The range must not cut a number of a run.
constexpr void reverse_elements_of_runs(u8* bytes, u32 range_begin, u32 range_end, ConvertedRuns const& runs, u32 buffer_size)
{
    for (auto const& converted : runs)
        reverse_elements_of_run(bytes, range_begin, range_end, converted.run, buffer_size);
}

// The contents of a buffer were replaced as a whole: the runs are taken to go on in the new contents, as a guess.
constexpr void keep_runs_for_new_contents(ConvertedRuns& runs, u32 old_buffer_size)
{
    for (auto& converted : runs) {
        converted.confirmed_end = 0;
        if (converted.run.end >= old_buffer_size)
            converted.run.end = ElementRun::to_the_end_of_the_buffer;
    }
}

constexpr bool have_the_same_pattern(ElementRun const& first, ElementRun const& second)
{
    if (first.stride != second.stride || first.element_size != second.element_size || first.element_count != second.element_count)
        return false;
    auto distance = first.offset > second.offset ? first.offset - second.offset : second.offset - first.offset;
    return distance % first.stride == 0;
}

// Whether OpenGL has the numbers of the wanted runs in the byte order of the host, and their bytes as they are.
constexpr bool are_in_order(ConvertedRuns& converted_runs, WantedRuns const& wanted, u32 buffer_size)
{
    for (auto const& run : wanted) {
        if (run.element_size < 2) {
            for (auto const& converted : converted_runs) {
                if (converted.has_numbers() && relation_of(run, converted.run, buffer_size).clash)
                    return false;
            }
            continue;
        }

        auto covered = false;
        for (auto& converted : converted_runs) {
            if (run_is_covered_by(run, converted.run, buffer_size)) {
                auto end = run.end < buffer_size ? run.end : buffer_size;
                if (converted.confirmed_end < end)
                    converted.confirmed_end = end;
                covered = true;
                break;
            }
        }
        if (!covered)
            return false;
    }
    return true;
}

// Takes the known runs out of the way of a wanted one that none of them covers. Returns true when numbers that
// were reversed are not any more.
constexpr bool make_room_for(ConvertedRuns& converted_runs, ElementRun& run, u32& read_up_to, u32 buffer_size)
{
    auto fewer_numbers = false;
    for (auto i = converted_runs.size(); i > 0; --i) {
        auto& converted = converted_runs[i - 1];
        if (run.element_size < 2 && !converted.has_numbers())
            continue;
        auto relation = relation_of(run, converted.run, buffer_size);
        if (!relation.clash && !relation.share)
            continue;

        if (!relation.clash && have_the_same_pattern(run, converted.run)) {
            // The same numbers over another part of the buffer: the wanted run takes that part along.
            if (converted.run.offset < run.offset)
                run.offset = converted.run.offset;
            if (converted.run.end > run.end)
                run.end = converted.run.end < buffer_size ? converted.run.end : buffer_size;
            if (converted.confirmed_end > read_up_to)
                read_up_to = converted.confirmed_end;
            converted_runs.remove(i - 1);
            continue;
        }

        if (converted.has_numbers())
            fewer_numbers = true;
        // What is drawn now is right. A run in the way goes, or ends where the wanted one starts if only what
        // was guessed of it is in the way.
        if (run.offset >= converted.confirmed_end && run.offset > converted.run.offset)
            converted.run.end = run.offset;
        else
            converted_runs.remove(i - 1);
    }
    return fewer_numbers;
}

// Brings the runs of a buffer to what a draw call reads. Returns true when numbers are reversed that were not, or
// the other way round, and OpenGL needs the contents of the buffer anew.
constexpr bool arrange_runs(ConvertedRuns& converted_runs, WantedRuns const& wanted, u32 buffer_size)
{
    auto new_copy = false;

    // A wanted run that gets room can take it from a run that covered another wanted one, so this is done until
    // nothing changes; that is the second time round, or the third.
    for (auto pass = 0; pass < 6; ++pass) {
        auto changed = false;

        for (auto run : wanted) {
            if (run.end > buffer_size)
                run.end = buffer_size;
            if (run.offset >= run.end)
                continue;
            auto read_up_to = run.end;

            if (run.element_size < 2) {
                auto in_the_way = false;
                auto known = false;
                for (auto const& converted : converted_runs) {
                    if (converted.has_numbers()) {
                        if (relation_of(run, converted.run, buffer_size).clash)
                            in_the_way = true;
                    } else if (have_the_same_pattern(run, converted.run) && converted.run.offset <= run.offset && converted.run.end >= run.end) {
                        known = true;
                    }
                }
                if (in_the_way) {
                    make_room_for(converted_runs, run, read_up_to, buffer_size);
                    new_copy = true;
                    changed = true;
                }
                if (!known && !converted_runs.is_full())
                    converted_runs.append({ .run = run, .confirmed_end = run.end, .guessed = false });
                continue;
            }

            auto covered = false;
            for (auto& converted : converted_runs) {
                if (run_is_covered_by(run, converted.run, buffer_size)) {
                    if (converted.confirmed_end < read_up_to)
                        converted.confirmed_end = read_up_to;
                    covered = true;
                    break;
                }
            }
            if (covered)
                continue;

            make_room_for(converted_runs, run, read_up_to, buffer_size);

            // The numbers are taken to go on behind what the draw call reads, as far as nothing known is there.
            auto grown = run;
            grown.end = ElementRun::to_the_end_of_the_buffer;
            for (auto const& converted : converted_runs) {
                auto relation = relation_of(grown, converted.run, buffer_size);
                if (!relation.clash && !relation.share)
                    continue;
                if (converted.run.offset >= run.end) {
                    if (converted.run.offset < grown.end)
                        grown.end = converted.run.offset;
                } else {
                    grown.end = run.end;
                }
            }
            if (grown.end < run.end)
                grown.end = run.end;

            if (converted_runs.is_full()) {
                // More kinds of numbers than there is room for: forget where bytes were read, then everything.
                for (auto i = converted_runs.size(); i > 0; --i) {
                    if (!converted_runs[i - 1].has_numbers())
                        converted_runs.remove(i - 1);
                }
                if (converted_runs.is_full())
                    converted_runs.clear();
                grown.end = run.end;
            }
            converted_runs.append({ .run = grown, .confirmed_end = read_up_to, .guessed = false });
            new_copy = true;
            changed = true;
        }

        if (!changed)
            break;
    }

    return new_copy;
}

}
