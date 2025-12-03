#version 450

#include "xgeom_static_mb_input_full.vert"
#include "mb_varying_definition_full.glsl"

// Mesh-level uniforms (updated once per mesh / per frame)
layout(set=2, binding = 0) uniform MeshUniforms
{
    mat4 L2w;          // Local space -> camera-centered small world
    mat4 w2C;          // Small world -> clip space (projection * view)
    mat4 L2CShadowT;   // Local space -> shadow texture space
} mesh;


// Fragment shader inputs
layout(location = 0) out VaryingFull Out;


void main()
{
    const mb_full_vertex VertData = getVertexData();

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

    Out.VertColor = vec4(1.,1.,1.,1.);
}

