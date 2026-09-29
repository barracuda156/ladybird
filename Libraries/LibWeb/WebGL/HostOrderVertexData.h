/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/Vector.h>
#include <LibWeb/WebGL/HostByteOrder.h>

namespace Web::WebGL {

// Gives the contents of vertex and index buffers to OpenGL with their numbers in the byte order of the host, on
// hosts where that is not the byte order of typed arrays (see HostByteOrder.h).
//
// A buffer is bytes without types when the page stores it. Which of them are numbers, and of what size, is said by
// the vertex attributes and index types that read the buffer when something is drawn. So the contents are kept as
// the page stored them, OpenGL gets a copy in which the numbers known so far are reversed, and a draw call that
// reads the buffer in a way the copy is not made for has the copy made again. After the first draw call this
// happens only when a page changes how it reads a buffer.
//
// Every function expects the OpenGL context of the buffers to be current. The ones that return an error make the
// OpenGL call themselves and return what glGetError() says after it; an error that OpenGL had before must have
// been taken from it.
class HostOrderVertexData {
    AK_MAKE_NONCOPYABLE(HostOrderVertexData);
    AK_MAKE_NONMOVABLE(HostOrderVertexData);

public:
    using Name = u32;
    using Error = u32;

    HostOrderVertexData() = default;
    ~HostOrderVertexData() = default;

    void bind_buffer(u32 target, Name);
    void delete_buffer(Name);

    // element_size_of_the_source is the size of the elements of the typed array that the bytes came in, or 1. It
    // stands in for the vertex attributes until something is drawn from the buffer.
    Error buffer_data(u32 target, ReadonlyBytes page_bytes, size_t element_size_of_the_source, u32 usage);
    Error buffer_data(u32 target, size_t size, u32 usage);
    Error buffer_sub_data(u32 target, size_t offset, ReadonlyBytes page_bytes);

    void bind_vertex_array(Name);
    void delete_vertex_array(Name);

    // To be called after OpenGL has accepted the call.
    void set_vertex_attribute_array_enabled(u32 index, bool);
    void set_vertex_attribute_pointer(u32 index, i32 size, u32 type, i32 stride, size_t offset);

    // To be called before the draw call.
    void prepare_to_draw_arrays(i32 first, i32 count);
    void prepare_to_draw_elements(i32 count, u32 type, size_t offset);
    // With instances, an attribute can be read once for every instance and not for every vertex: it is taken
    // to be read as far as its buffer goes.
    void prepare_to_draw_arrays_instanced();
    void prepare_to_draw_elements_instanced(i32 count, u32 type, size_t offset);

private:
    struct VertexRange {
        u32 first { 0 };
        u32 last { 0 };
    };

    struct IndexRange {
        ElementRun indices;
        VertexRange vertices;
    };

    struct Buffer {
        ByteBuffer page_bytes;
        ConvertedRuns runs;
        // Changes when what a draw call finds in the buffer may have changed: the runs, the size, and the contents
        // if they are indices.
        u32 generation { 0 };
        // The vertices that the last draw calls with this buffer as indices have read.
        Vector<IndexRange, 8> index_ranges;
    };

    struct Attribute {
        bool enabled { false };
        Name buffer { 0 };
        ElementRun run;
    };

    struct PreparedBuffer {
        Name name { 0 };
        u32 generation { 0 };

        bool operator==(PreparedBuffer const&) const = default;
    };

    struct Draw {
        bool with_indices { false };
        bool with_instances { false };
        // The indices, or the first vertex and the number of vertices in offset and end.
        ElementRun what;

        bool operator==(Draw const&) const = default;
    };

    struct VertexArray {
        Vector<Attribute, 16> attributes;
        Name element_buffer { 0 };
        u32 generation { 0 };

        // What the last preparation found, to do nothing when it finds the same again.
        bool prepared { false };
        u32 prepared_generation { 0 };
        Draw prepared_draw;
        Vector<PreparedBuffer, 17> prepared_buffers;
    };

    static constexpr u32 highest_attribute_index = 63;

    Buffer* buffer_bound_to(u32 target);
    Buffer* buffer_named(Name);
    VertexArray& current_vertex_array();

    void prepare(Draw const&);
    void bring_in_order(Name, Buffer&, WantedRuns const&, bool holds_indices);
    static Optional<VertexRange> vertices_read_through(Buffer&, ElementRun const& indices);
    static ErrorOr<ByteBuffer> in_host_order(ReadonlyBytes page_bytes, ConvertedRuns const&, u32 begin, u32 end);

    HashMap<Name, NonnullOwnPtr<Buffer>> m_buffers;
    HashMap<Name, NonnullOwnPtr<VertexArray>> m_vertex_arrays;
    Name m_array_buffer { 0 };
    Name m_vertex_array { 0 };
};

}
