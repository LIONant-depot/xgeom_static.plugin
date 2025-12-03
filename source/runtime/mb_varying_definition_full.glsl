struct VaryingFull
{
    mat3 T2w;               // Tangent to small-world (rotation only)
    vec4 wSpacePosition;    // Position in small world
    vec4 ShadowPosition;    // Position in shadow texture-clip-space
    vec4 VertColor;         // Color of the vertices
    vec2 UV;                // Final texture coordinates
};
