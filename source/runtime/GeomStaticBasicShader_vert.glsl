#version 450

#include "xgeom_static_mb_standard_vert.glsl"


// Mesh-level uniforms (updated once per mesh / per frame)
layout(set=2, binding = 0) uniform MeshUniforms
{
    mat4 L2w;          // Local space -> camera-centered small world
    mat4 w2C;          // Small world -> clip space (projection * view)
    mat4 L2CShadowT;   // Local space -> shadow texture space
    vec4 LightColor;
    vec4 AmbientLightColor;
    vec4 wSpaceLightPos;
    vec4 wSpaceEyePos;

} mesh;


// Fragment shader inputs
layout(location = 0) out struct _o
{
    mat3 T2w;             // Tangent to small-world (rotation only)
    vec4 wSpacePosition;  // Position in small world
    vec4 ShadowPosition;  // Position in shadow texture-clip-space
    vec2 UV;              // Final texture coordinates
} Out;


void main()
{
    const vertex_data VertData = getVertexData();

    //
    // Transform to all needed spaces
    //
    Out.wSpacePosition      = mesh.L2w        * VertData.LocalPos;
    Out.ShadowPosition      = mesh.L2CShadowT * VertData.LocalPos;
    gl_Position             = mesh.w2C        * Out.wSpacePosition;

    //
    // Set the UVs
    //
    Out.UV = VertData.UV;

    //
    //  Tanget to Worls matrix
    // 
    
    // Build rotation-only matrix from L2w (strips scaling/shear)
    const mat3 L2w_rot = mat3(normalize(mesh.L2w[0].xyz),
                              normalize(mesh.L2w[1].xyz),
                              normalize(mesh.L2w[2].xyz));

    // Optional: Gram-Schmidt to orthonormalize T
    // Tangent = normalize(Tangent - Normal * dot(Normal, Tangent));

    // Tangent-to-world matrix (rotation only)
    Out.T2w = L2w_rot * mat3(VertData.Tangent, VertData.Binormal, VertData.Normal);
}

