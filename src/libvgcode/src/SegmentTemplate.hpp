///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ libvgcode is released under the terms of the AGPLv3 or higher
///|/
#ifndef VGCODE_SEGMENTTEMPLATE_HPP
#define VGCODE_SEGMENTTEMPLATE_HPP

#include <cstddef>

namespace libvgcode {

class SegmentTemplate
{
public:
    // [INTENT] Default constructor
    // [UNITY] Simple struct or class with default initialization
    SegmentTemplate() = default;

    // [INTENT] Destructor ensures cleanup
    // [UNITY] Implement IDisposable pattern or use try-finally blocks
    ~SegmentTemplate() { shutdown(); }

    // [INTENT] Delete copy constructor - resource management pattern
    // [PORTING_HAZARD] Non-copyable pattern requires special handling in C#
    SegmentTemplate(const SegmentTemplate& other) = delete;

    // [INTENT] Delete move constructor
    // [PORTING_HAZARD] Non-movable special handling needed
    SegmentTemplate(SegmentTemplate&& other) = delete;

    // [INTENT] Delete copy assignment
    // [PORTING_HAZARD] Non-copyable - make readonly or implement manual Clone()
    SegmentTemplate& operator=(const SegmentTemplate& other) = delete;

    // [INTENT] Delete move assignment
    // [PORTING_HAZARD] Non-movable - requires custom implementation
    SegmentTemplate& operator=(const SegmentTemplate&& other) = delete;

    //
    // [INTENT] Initialize gpu buffers.
    // [OPENGL] Creates VAO/VBO GPU resources
    // [UNITY] Use ComputeBuffer or GraphicsBuffer in C# (Native plugin pattern)
    // [PORTING_HAZARD:P2] GPU resource management requires Unity-specific implementation
    //
    void init();

    //
    // [INTENT] Release gpu buffers.
    // [OPENGL] Deletes VAO/VBO GPU resources
    // [UNITY] Call GraphicsBuffer.Release() or similar
    // [PORTING_HAZARD:P2] Must be called before object destruction to avoid GPU leaks
    //
    void shutdown();

    // [INTENT] Render count instances with template geometry
    // [OPENGL] Draws instanced geometry (likely axis-aligned markers/segments)
    // [UNITY] Use Graphics.DrawMeshInstanced or CommandBuffer.DrawRenderer
    // [PORTING_HAZARD:P3] Rendering pipeline needs complete Unity replacement
    void render(size_t count);

    //
    // [INTENT] Return the size of the data sent to gpu, in bytes.
    // [STATE] Query only, does not modify state
    // [UNITY] Simple getter property
    //
    size_t size_in_bytes_gpu() const { return m_size_in_bytes_gpu; }

private:
    //
    // [STATE] GPU buffer IDs (VAO = Vertex Array Object, VBO = Vertex Buffer Object)
    // [OPENGL] Native OpenGL handles
    // [UNITY] These become managed GraphicsBuffer/ComputeBuffer handles
    // [PORTING_HAZARD:P2] Private fields - make internal for plugin access or expose in API
    //
    unsigned int m_vao_id{0};
    unsigned int m_vbo_id{0};

    //
    // [STATE] Cached size of gpu buffer data
    // [UNITY] Cache and invalidate appropriately in C# implementation
    //
    size_t m_size_in_bytes_gpu{0};
};

} // namespace libvgcode

#endif // VGCODE_SEGMENTTEMPLATE_HPP