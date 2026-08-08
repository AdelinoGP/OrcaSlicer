#version 110

// PNP fork (ADR-0002): slice-progress fragment shader. Samples the per-layer
// status LUT (1D texture, one texel per global plate layer, colored by
// LayerStatus) at the layer index derived from the plate-absolute Z, and
// multiplies it by the standard two-light intensity so the model still reads
// as 3D.

// 1D LUT of per-layer status colors (texel i = layer i).
uniform sampler1D lut;
// Layer index = (object_z - z_min) * z_inv_range * layer_count.
uniform float z_min;
uniform float z_inv_range;
uniform float layer_count;

// x = tainted, y = specular;
varying vec2 intensity;

varying float object_z;

void main()
{
    float layer = (object_z - z_min) * z_inv_range * layer_count;
    // NEAREST sampling: texel i covers [(i - 0.5) / N, (i + 0.5) / N).
    // CLAMP_TO_EDGE keeps out-of-range Z (below the bed, above the tallest
    // object) on the first/last texel.
    vec4 color = texture1D(lut, (layer + 0.5) / layer_count);
    gl_FragColor = vec4(vec3(intensity.y), 1.0) + intensity.x * color;
}
