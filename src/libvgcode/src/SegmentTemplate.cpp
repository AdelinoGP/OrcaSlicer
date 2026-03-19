///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#include "SegmentTemplate.hpp"
#include "OpenGLUtils.hpp"

#include <cstdint>
#include <array>

namespace libvgcode {

// [INTENT] Predefined vertex data for segment marker geometry
// [STATE] Static constant - immutable template data
// [UNITY] Can be const array in C#: static readonly byte[] VERTEX_DATA
// This creates an hourglass-like quad geometry for segment markers
//|     /1-------6\     |
//|    / |       | \    |
//|   2--0-------5--7   |
//|    \ |       | /    |
//|      3-------4      |
static constexpr const std::array<uint8_t, 24> VERTEX_DATA = {
    0, 1, 2, // front spike
    0, 2, 3, // front spike
    0, 3, 4, // right/bottom body
    0, 4, 5, // right/bottom body
    0, 5, 6, // left/top body
    0, 6, 1, // left/top body
    5, 4, 7, // back spike
    5, 7, 6, // back spike
};

void SegmentTemplate::init()
{
    // [INTENT] Guard against double-initialization
    // [STATE] Checks m_vao_id != 0
    if (m_vao_id != 0)
        return;

    // [INTENT] Track GPU memory usage for debugging/profiling
    // [STATE] Updates m_size_in_bytes_gpu (field in class)
    m_size_in_bytes_gpu += VERTEX_DATA.size() * sizeof(uint8_t);

    // [INTENT] Save current OpenGL state to restore later
    // [OPENGL] State preservation pattern
    // [PORTING_HAZARD] OpenGL state machine doesn't exist in Unity - remove pattern
    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));
    int curr_array_buffer;
    glsafe(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &curr_array_buffer));

    // [INTENT] Create vertex array object and vertex buffer object
    // [OPENGL] VAO/VBO creation
    // [UNITY] Replace with GraphicsBuffer / ComputeBuffer creation
    glsafe(glGenVertexArrays(1, &m_vao_id));
    glsafe(glBindVertexArray(m_vao_id));

    glsafe(glGenBuffers(1, &m_vbo_id));
    glsafe(glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));

    // [INTENT] Upload index data to GPU
    // [OPENGL] Static draw means immutable geometry
    // [UNITY] ComputeBuffer.SetData() with ComputeBufferType.Raw
    glsafe(glBufferData(GL_ARRAY_BUFFER, VERTEX_DATA.size() * sizeof(uint8_t), VERTEX_DATA.data(), GL_STATIC_DRAW));

    // [INTENT] Configure vertex attribute layout
    // [OPENGL] Attribute location 0, single byte
    // [UNITY] Input layout in shader would need unpacking or byte4 support
    glsafe(glEnableVertexAttribArray(0));
#ifdef ENABLE_OPENGL_ES
    glsafe(glVertexAttribPointer(0, 1, GL_UNSIGNED_BYTE, GL_FALSE, 0, (const void*) 0));
#else
    glsafe(glVertexAttribIPointer(0, 1, GL_UNSIGNED_BYTE, 0, (const void*) 0));
#endif // ENABLE_OPENGL_ES

    // [INTENT] Restore OpenGL state
    // [OPENGL] Cleanup from preserve/restore pattern
    glsafe(glBindBuffer(GL_ARRAY_BUFFER, curr_array_buffer));
    glsafe(glBindVertexArray(curr_vertex_array));
}

void SegmentTemplate::shutdown()
{
    // [INTENT] Release GPU resources to prevent memory leaks
    // [OPENGL] Delete VBO/VAO
    // [UNITY] Call GraphicsBuffer.Release() or similar
    if (m_vbo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }
    if (m_vao_id != 0) {
        glsafe(glDeleteVertexArrays(1, &m_vao_id));
        m_vao_id = 0;
    }

    // [INTENT] Reset tracking counter
    // [STATE] Update size field
    m_size_in_bytes_gpu = 0;
}

void SegmentTemplate::render(size_t count)
{
    // [INTENT] Guard against invalid state
    // [OPENGL] Early exit for invalid/zero count
    if (m_vao_id == 0 || m_vbo_id == 0 || count == 0)
        return;

    // [INTENT] State preservation
    // [PORTING_HAZARD] Pattern will need complete rework in Unity
    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));

    // [INTENT] Bind template geometry and draw N instances
    // [OPENGL] Instanced rendering for N markers
    // [UNITY] Graphics.DrawMeshInstanced or CommandBuffer equivalent
    glsafe(glBindVertexArray(m_vao_id));
    glsafe(glDrawArraysInstanced(GL_TRIANGLES, 0, static_cast<GLsizei>(VERTEX_DATA.size()), static_cast<GLsizei>(count)));
    glsafe(glBindVertexArray(curr_vertex_array));
}

} // namespace libvgcode