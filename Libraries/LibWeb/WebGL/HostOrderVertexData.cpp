/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Checked.h>
#include <AK/Debug.h>
#include <AK/Format.h>
#include <LibWeb/WebGL/HostOrderVertexData.h>

#include <GLES2/gl2.h>

namespace Web::WebGL {

static constexpr u32 largest_buffer = 0xfffffffeu;

static bool is_tracked_target(u32 target)
{
    return target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER;
}

static u32 clamped_to_u32(u64 value)
{
    return value > largest_buffer ? largest_buffer : static_cast<u32>(value);
}

HostOrderVertexData::VertexArray& HostOrderVertexData::current_vertex_array()
{
    if (auto vertex_array = m_vertex_arrays.get(m_vertex_array); vertex_array.has_value())
        return *vertex_array.value();
    m_vertex_arrays.set(m_vertex_array, make<VertexArray>());
    return *m_vertex_arrays.get(m_vertex_array).value();
}

HostOrderVertexData::Buffer* HostOrderVertexData::buffer_named(Name name)
{
    if (name == 0)
        return nullptr;
    auto buffer = m_buffers.get(name);
    if (!buffer.has_value())
        return nullptr;
    return buffer.value();
}

HostOrderVertexData::Buffer* HostOrderVertexData::buffer_bound_to(u32 target)
{
    if (target == GL_ARRAY_BUFFER)
        return buffer_named(m_array_buffer);
    if (target == GL_ELEMENT_ARRAY_BUFFER)
        return buffer_named(current_vertex_array().element_buffer);
    return nullptr;
}

void HostOrderVertexData::bind_buffer(u32 target, Name name)
{
    if (!is_tracked_target(target))
        return;

    if (name != 0 && !m_buffers.contains(name))
        m_buffers.set(name, make<Buffer>());

    if (target == GL_ARRAY_BUFFER) {
        m_array_buffer = name;
        return;
    }

    auto& vertex_array = current_vertex_array();
    if (vertex_array.element_buffer != name) {
        vertex_array.element_buffer = name;
        ++vertex_array.generation;
    }
}

void HostOrderVertexData::delete_buffer(Name name)
{
    if (name == 0 || !m_buffers.remove(name))
        return;

    // OpenGL takes a deleted buffer out of the bindings of the context and of the vertex array that is bound. Other
    // vertex arrays keep it, but nothing can be stored in it any more, and the name may go to another buffer: they
    // lose it here as well, which leaves its contents as the last draw call wanted them.
    if (m_array_buffer == name)
        m_array_buffer = 0;
    for (auto& entry : m_vertex_arrays) {
        auto& vertex_array = *entry.value;
        if (vertex_array.element_buffer == name) {
            vertex_array.element_buffer = 0;
            ++vertex_array.generation;
        }
        for (auto& attribute : vertex_array.attributes) {
            if (attribute.buffer == name) {
                attribute.buffer = 0;
                ++vertex_array.generation;
            }
        }
    }
}

// The bytes [begin, end) of a buffer as OpenGL is to have them. No number of a run is cut by the range.
ErrorOr<ByteBuffer> HostOrderVertexData::in_host_order(ReadonlyBytes page_bytes, ConvertedRuns const& runs, u32 begin, u32 end)
{
    auto bytes = TRY(ByteBuffer::copy(page_bytes.slice(begin, end - begin)));
    reverse_elements_of_runs(bytes.data(), begin, end, runs, clamped_to_u32(page_bytes.size()));
    return bytes;
}

HostOrderVertexData::Error HostOrderVertexData::buffer_data(u32 target, ReadonlyBytes page_bytes, size_t element_size_of_the_source, u32 usage)
{
    auto* buffer = is_tracked_target(target) ? buffer_bound_to(target) : nullptr;
    if (!buffer || page_bytes.size() > largest_buffer) {
        // Nothing of ours is bound: OpenGL says what is wrong with the call.
        glBufferData(target, page_bytes.size(), page_bytes.data(), usage);
        return glGetError();
    }

    auto kept_page_bytes = ByteBuffer::copy(page_bytes);
    if (kept_page_bytes.is_error())
        return GL_OUT_OF_MEMORY;

    // The buffer is most likely read as before. Until something is drawn from a new one, the elements of the typed
    // array that the page stored are the best guess at its numbers.
    auto runs = buffer->runs;
    keep_runs_for_new_contents(runs, clamped_to_u32(buffer->page_bytes.size()));
    if (runs.is_empty() && (element_size_of_the_source == 2 || element_size_of_the_source == 4)) {
        ElementRun run;
        run.stride = element_size_of_the_source;
        run.element_size = element_size_of_the_source;
        runs.append({ .run = run, .confirmed_end = 0, .guessed = true });
    }

    auto host_bytes = in_host_order(page_bytes, runs, 0, clamped_to_u32(page_bytes.size()));
    if (host_bytes.is_error())
        return GL_OUT_OF_MEMORY;

    glBufferData(target, host_bytes.value().size(), host_bytes.value().data(), usage);
    if (auto error = glGetError(); error != GL_NO_ERROR)
        return error;

    buffer->page_bytes = kept_page_bytes.release_value();
    buffer->runs = runs;
    buffer->index_ranges.clear_with_capacity();
    ++buffer->generation;
    return GL_NO_ERROR;
}

HostOrderVertexData::Error HostOrderVertexData::buffer_data(u32 target, size_t size, u32 usage)
{
    auto* buffer = is_tracked_target(target) ? buffer_bound_to(target) : nullptr;
    if (!buffer || size > largest_buffer) {
        glBufferData(target, size, nullptr, usage);
        return glGetError();
    }

    auto page_bytes = ByteBuffer::create_zeroed(size);
    if (page_bytes.is_error())
        return GL_OUT_OF_MEMORY;

    glBufferData(target, size, nullptr, usage);
    if (auto error = glGetError(); error != GL_NO_ERROR)
        return error;

    keep_runs_for_new_contents(buffer->runs, clamped_to_u32(buffer->page_bytes.size()));
    buffer->page_bytes = page_bytes.release_value();
    buffer->index_ranges.clear_with_capacity();
    ++buffer->generation;
    return GL_NO_ERROR;
}

HostOrderVertexData::Error HostOrderVertexData::buffer_sub_data(u32 target, size_t offset, ReadonlyBytes page_bytes)
{
    auto* buffer = is_tracked_target(target) ? buffer_bound_to(target) : nullptr;
    auto end = Checked<size_t> { offset };
    end += page_bytes.size();
    if (!buffer || end.has_overflow() || end.value() > buffer->page_bytes.size()) {
        glBufferSubData(target, offset, page_bytes.size(), page_bytes.data());
        return glGetError();
    }
    if (page_bytes.is_empty())
        return GL_NO_ERROR;

    // The numbers that the new bytes are part of are made of old and new bytes. Put the new bytes where the page
    // has them, then hand every number that has changed to OpenGL whole.
    auto size = clamped_to_u32(buffer->page_bytes.size());
    Vector<u8> old_bytes;
    if (old_bytes.try_append(buffer->page_bytes.data() + offset, page_bytes.size()).is_error())
        return GL_OUT_OF_MEMORY;
    buffer->page_bytes.overwrite(offset, page_bytes.data(), page_bytes.size());

    auto range_begin = static_cast<u32>(offset);
    auto range_end = static_cast<u32>(end.value());
    for (auto const& converted : buffer->runs)
        widen_to_whole_elements(range_begin, range_end, converted.run, size);

    auto host_bytes = in_host_order(buffer->page_bytes.bytes(), buffer->runs, range_begin, range_end);
    if (host_bytes.is_error()) {
        buffer->page_bytes.overwrite(offset, old_bytes.data(), old_bytes.size());
        return GL_OUT_OF_MEMORY;
    }

    glBufferSubData(target, range_begin, host_bytes.value().size(), host_bytes.value().data());
    if (auto error = glGetError(); error != GL_NO_ERROR) {
        buffer->page_bytes.overwrite(offset, old_bytes.data(), old_bytes.size());
        return error;
    }

    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        // Other indices can read other vertices.
        buffer->index_ranges.clear_with_capacity();
        ++buffer->generation;
    }
    return GL_NO_ERROR;
}

void HostOrderVertexData::bind_vertex_array(Name name)
{
    m_vertex_array = name;
}

void HostOrderVertexData::delete_vertex_array(Name name)
{
    if (name == 0)
        return;
    m_vertex_arrays.remove(name);
    if (m_vertex_array == name)
        m_vertex_array = 0;
}

void HostOrderVertexData::set_vertex_attribute_array_enabled(u32 index, bool enabled)
{
    if (index > highest_attribute_index)
        return;
    auto& vertex_array = current_vertex_array();
    if (vertex_array.attributes.size() <= index)
        vertex_array.attributes.resize(index + 1);
    auto& attribute = vertex_array.attributes[index];
    if (attribute.enabled == enabled)
        return;
    attribute.enabled = enabled;
    ++vertex_array.generation;
}

void HostOrderVertexData::set_vertex_attribute_pointer(u32 index, i32 size, u32 type, i32 stride, size_t offset)
{
    if (index > highest_attribute_index || size < 1 || size > 4 || stride < 0)
        return;
    auto& vertex_array = current_vertex_array();
    if (vertex_array.attributes.size() <= index)
        vertex_array.attributes.resize(index + 1);

    ElementRun run;
    run.element_size = element_size_of_vertex_type(type);
    run.element_count = is_packed_vertex_type(type) ? 1 : size;
    run.stride = stride != 0 ? static_cast<u32>(stride) : run.bytes_per_vertex();
    run.offset = clamped_to_u32(offset);

    auto& attribute = vertex_array.attributes[index];
    if (attribute.buffer == m_array_buffer && attribute.run == run)
        return;
    attribute.buffer = m_array_buffer;
    attribute.run = run;
    ++vertex_array.generation;
}

void HostOrderVertexData::prepare_to_draw_arrays(i32 first, i32 count)
{
    if (first < 0 || count <= 0)
        return;
    Draw draw;
    draw.what.offset = static_cast<u32>(first);
    draw.what.end = static_cast<u32>(count);
    prepare(draw);
}

void HostOrderVertexData::prepare_to_draw_elements(i32 count, u32 type, size_t offset)
{
    if (count <= 0)
        return;
    Draw draw;
    draw.with_indices = true;
    draw.what.element_size = element_size_of_vertex_type(type);
    draw.what.stride = draw.what.element_size;
    draw.what.offset = clamped_to_u32(offset);
    draw.what.end = clamped_to_u32(static_cast<u64>(draw.what.offset) + static_cast<u64>(count) * draw.what.element_size);
    prepare(draw);
}

void HostOrderVertexData::prepare_to_draw_arrays_instanced()
{
    Draw draw;
    draw.with_instances = true;
    prepare(draw);
}

void HostOrderVertexData::prepare_to_draw_elements_instanced(i32 count, u32 type, size_t offset)
{
    if (count <= 0)
        return;
    Draw draw;
    draw.with_indices = true;
    draw.with_instances = true;
    draw.what.element_size = element_size_of_vertex_type(type);
    draw.what.stride = draw.what.element_size;
    draw.what.offset = clamped_to_u32(offset);
    draw.what.end = clamped_to_u32(static_cast<u64>(draw.what.offset) + static_cast<u64>(count) * draw.what.element_size);
    prepare(draw);
}

// The first and the last vertex that the indices name. They are in the byte order of the page here.
Optional<HostOrderVertexData::VertexRange> HostOrderVertexData::vertices_read_through(Buffer& buffer, ElementRun const& indices)
{
    for (auto const& known : buffer.index_ranges) {
        if (known.indices == indices)
            return known.vertices;
    }

    auto end = indices.end < buffer.page_bytes.size() ? indices.end : clamped_to_u32(buffer.page_bytes.size());
    if (indices.offset >= end || end - indices.offset < indices.element_size)
        return {};

    auto const* bytes = buffer.page_bytes.data();
    VertexRange vertices { 0xffffffff, 0 };
    for (auto at = indices.offset; end - at >= indices.element_size; at += indices.element_size) {
        u32 index = bytes[at];
        if (indices.element_size >= 2)
            index |= static_cast<u32>(bytes[at + 1]) << 8;
        if (indices.element_size == 4)
            index |= static_cast<u32>(bytes[at + 2]) << 16 | static_cast<u32>(bytes[at + 3]) << 24;
        if (index < vertices.first)
            vertices.first = index;
        if (index > vertices.last)
            vertices.last = index;
    }

    if (buffer.index_ranges.size() == buffer.index_ranges.capacity())
        buffer.index_ranges.remove(0);
    buffer.index_ranges.append({ indices, vertices });
    return vertices;
}

void HostOrderVertexData::prepare(Draw const& draw)
{
    auto& vertex_array = current_vertex_array();

    Vector<PreparedBuffer, 17> buffers_in_use;
    auto use = [&](Name name) {
        auto* buffer = buffer_named(name);
        if (!buffer)
            return;
        PreparedBuffer in_use { name, buffer->generation };
        if (!buffers_in_use.contains_slow(in_use))
            buffers_in_use.append(in_use);
    };

    for (auto const& attribute : vertex_array.attributes) {
        if (attribute.enabled)
            use(attribute.buffer);
    }
    auto const indices_name = draw.with_indices ? vertex_array.element_buffer : 0;
    use(indices_name);

    if (vertex_array.prepared
        && vertex_array.prepared_generation == vertex_array.generation
        && vertex_array.prepared_draw == draw
        && vertex_array.prepared_buffers == buffers_in_use)
        return;

    // The indices first: the vertices that are read are in them.
    VertexRange vertices { 0, 0xffffffff };
    if (!draw.with_indices && !draw.with_instances)
        vertices = { draw.what.offset, draw.what.offset + (draw.what.end - 1) };
    if (draw.with_indices) {
        auto* indices_buffer = buffer_named(indices_name);
        if (!indices_buffer)
            return;
        WantedRuns wanted;
        wanted.append(draw.what);
        bring_in_order(indices_name, *indices_buffer, wanted, true);

        if (!draw.with_instances) {
            auto vertices_of_the_indices = vertices_read_through(*indices_buffer, draw.what);
            if (!vertices_of_the_indices.has_value())
                return;
            vertices = vertices_of_the_indices.value();
        }
    }

    for (auto const& in_use : buffers_in_use) {
        if (in_use.name == indices_name)
            continue;
        auto* buffer = buffer_named(in_use.name);
        VERIFY(buffer);

        WantedRuns wanted;
        for (auto const& attribute : vertex_array.attributes) {
            if (!attribute.enabled || attribute.buffer != in_use.name)
                continue;
            auto run = attribute.run;
            auto begin = static_cast<u64>(run.offset) + static_cast<u64>(vertices.first) * run.stride;
            auto end = static_cast<u64>(run.offset) + static_cast<u64>(vertices.last) * run.stride + run.bytes_per_vertex();
            if (begin > largest_buffer)
                continue;
            run.offset = static_cast<u32>(begin);
            run.end = clamped_to_u32(end);
            if (!wanted.contains(run))
                wanted.append(run);
        }
        bring_in_order(in_use.name, *buffer, wanted, false);
    }

    // The generations are the ones after the conversions: the same draw call again has nothing to do.
    vertex_array.prepared_buffers.clear_with_capacity();
    for (auto const& in_use : buffers_in_use)
        vertex_array.prepared_buffers.append({ in_use.name, buffer_named(in_use.name)->generation });
    vertex_array.prepared_generation = vertex_array.generation;
    vertex_array.prepared_draw = draw;
    vertex_array.prepared = true;
}

void HostOrderVertexData::bring_in_order(Name name, Buffer& buffer, WantedRuns const& wanted, bool holds_indices)
{
    if (buffer.page_bytes.is_empty() || wanted.is_empty())
        return;
    auto size = clamped_to_u32(buffer.page_bytes.size());

    if (!arrange_runs(buffer.runs, wanted, size))
        return;

    auto host_bytes = in_host_order(buffer.page_bytes.bytes(), buffer.runs, 0, size);
    if (host_bytes.is_error()) {
        dbgln("WebGL: No memory for the {} bytes of buffer {} in the byte order of the host", size, name);
        return;
    }

    // The buffer with the indices is bound, it is the one of the vertex array. The others go through the binding
    // for vertices, which is not part of what the draw call reads.
    if (holds_indices) {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, host_bytes.value().size(), host_bytes.value().data());
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, name);
        glBufferSubData(GL_ARRAY_BUFFER, 0, host_bytes.value().size(), host_bytes.value().data());
        glBindBuffer(GL_ARRAY_BUFFER, m_array_buffer);
    }
    ++buffer.generation;
}

}
