///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
// needed for tech VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "../include/Types.hpp"
#include "CogMarker.hpp"
#include "OpenGLUtils.hpp"
#include "Utils.hpp"

#include <cmath>
#include <assert.h>
#include <algorithm>

// [INTENT] CogMarker manages GPU resources for rendering a center-of-gravity marker (sphere geometry).
// [UNITY] Use a MonoBehaviour or ScriptableObject to manage procedural Mesh generation and rendering.
//         The `init` method should be replaced with Mesh construction (SetVertices, SetNormals, SetIndices).
//         `render` should call Graphics.DrawMesh or rely on the MeshRenderer component.

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS

namespace libvgcode {

// Geometry:
// sphere with 'resolution' sides, centered at (0.0, 0.0, 0.0) and radius equal to 'radius'
// [UNITY] Reimplement as a procedural sphere mesh generation method.
void CogMarker::init(uint8_t resolution, float radius)
{
    if (m_vao_id != 0)
        return;

    // ensure vertices count does not exceed 65536
    resolution = std::clamp<uint8_t>(resolution, 4, 105);

    const uint16_t sector_count = (uint16_t) resolution;
    const uint16_t stack_count  = (uint16_t) resolution;

    const float sector_step = 2.0f * PI / float(sector_count);
    const float stack_step  = PI / float(stack_count);

    std::vector<float> vertices;
    const uint16_t     vertices_count = (stack_count - 1) * sector_count + 2;
    vertices.reserve(6 * vertices_count);

    m_indices_count = 3 * (2 * (stack_count - 1) * sector_count);
    std::vector<uint16_t> indices;
    indices.reserve(m_indices_count);

    // vertices
    for (uint16_t i = 0; i <= stack_count; ++i) {
        // from pi/2 to -pi/2
        const float stack_angle = 0.5f * PI - stack_step * float(i);
        const float xy          = radius * std::cos(stack_angle);
        const float z           = radius * std::sin(stack_angle);
        if (i == 0 || i == stack_count) {
            const Vec3 pos  = {xy, 0.0f, z};
            const Vec3 norm = normalize(pos);
            add_vertex(pos, norm, vertices);
        } else {
            for (uint16_t j = 0; j < sector_count; ++j) {
                // from 0 to 2pi
                const float sector_angle = sector_step * float(j);
                const Vec3  pos          = {xy * std::cos(sector_angle), xy * std::sin(sector_angle), z};
                const Vec3  norm         = normalize(pos);
                add_vertex(pos, norm, vertices);
            }
        }
    }

    // indices
    for (uint16_t i = 0; i < stack_count; ++i) {
        // Beginning of current stack.
        uint16_t       k1       = (i == 0) ? 0 : (1 + (i - 1) * sector_count);
        const uint16_t k1_first = k1;
        // Beginning of next stack.
        uint16_t       k2       = (i == 0) ? 1 : (k1 + sector_count);
        const uint16_t k2_first = k2;
        for (uint16_t j = 0; j < sector_count; ++j) {
            // 2 triangles per sector excluding first and last stacks
            uint16_t k1_next = k1;
            uint16_t k2_next = k2;
            if (i != 0) {
                k1_next = (j + 1 == sector_count) ? k1_first : (k1 + 1);
                add_triangle(k1, k2, k1_next, indices);
            }
            if (i + 1 != stack_count) {
                k2_next = (j + 1 == sector_count) ? k2_first : (k2 + 1);
                add_triangle(k1_next, k2, k2_next, indices);
            }
            k1 = k1_next;
            k2 = k2_next;
        }
    }

    m_size_in_bytes_gpu += vertices.size() * sizeof(float);
    m_size_in_bytes_gpu += indices.size() * sizeof(uint16_t);

    const size_t vertex_stride   = 6 * sizeof(float);
    const size_t position_offset = 0;
    const size_t normal_offset   = 3 * sizeof(float);

    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));
    int curr_array_buffer;
    glsafe(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &curr_array_buffer));

    // [OPENGL] Buffer and VAO configuration using VBOs and IBOs.
    glsafe(glGenVertexArrays(1, &m_vao_id));
    glsafe(glBindVertexArray(m_vao_id));
    // ... rest of OpenGL init ...

    glsafe(glBindBuffer(GL_ARRAY_BUFFER, curr_array_buffer));
    glsafe(glBindVertexArray(curr_vertex_array));
}

// [OPENGL] Cleanup GPU resources.
// [UNITY] Not required; garbage collector manages object destruction, Mesh objects should be explicitly destroyed.
void CogMarker::shutdown()
{
    if (m_ibo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_ibo_id));
        m_ibo_id = 0;
    }
    // ... rest of shutdown ...
}

// [OPENGL] Draw the sphere geometry using glDrawElements.
// [UNITY] Use Graphics.DrawMesh or render through an attached MeshRenderer.
void CogMarker::render()
{
    if (m_vao_id == 0 || m_vbo_id == 0 || m_ibo_id == 0 || m_indices_count == 0)
        return;
    // ... draw elements ...
}

void CogMarker::shutdown()
{
    if (m_ibo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_ibo_id));
        m_ibo_id = 0;
    }
    if (m_vbo_id != 0) {
        glsafe(glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }
    if (m_vao_id != 0) {
        glsafe(glDeleteVertexArrays(1, &m_vao_id));
        m_vao_id = 0;
    }

    m_size_in_bytes_gpu = 0;
}

void CogMarker::render()
{
    if (m_vao_id == 0 || m_vbo_id == 0 || m_ibo_id == 0 || m_indices_count == 0)
        return;

    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));
    glcheck();

    glsafe(glBindVertexArray(m_vao_id));
    glsafe(glDrawElements(GL_TRIANGLES, m_indices_count, GL_UNSIGNED_SHORT, (const void*) 0));
    glsafe(glBindVertexArray(curr_vertex_array));
}

void CogMarker::update(const Vec3& position, float mass)
{
    m_total_position = m_total_position + mass * position;
    m_total_mass += mass;
}

void CogMarker::reset()
{
    m_total_position = {0.0f, 0.0f, 0.0f};
    m_total_mass     = 0.0f;
}

Vec3 CogMarker::get_position() const
{
    assert(m_total_mass > 0.0f);
    const float inv_total_mass = 1.0f / m_total_mass;
    return inv_total_mass * m_total_position;
}

} // namespace libvgcode

#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
