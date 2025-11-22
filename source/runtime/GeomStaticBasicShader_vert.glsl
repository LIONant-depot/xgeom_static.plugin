#version 450

// Fast oct-encoded normal/tangent decode (12-bit N, 12/11-bit T)
vec3 oct_decode(vec2 e)
{
    e = e * 2.0 - 1.0;                          // [0,1] -> [-1,1]
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
    {
        v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    }
    return normalize(v);
}

// Mesh-level uniforms (updated once per mesh / per frame)
layout(set=2, binding = 0) uniform MeshUniforms
{
    mat4 L2w;          // Local space -> camera-centered small world
    mat4 w2C;          // Small world -> clip space (projection * view)
    mat4 L2CShadow;    // Local space -> shadow clip space
    vec4 LightColor;
    vec4 AmbientLightColor;
    vec4 wSpaceLightPos;
    vec4 wSpaceEyePos;

} mesh;

// Cluster struct
struct ClusterData
{
    vec4 posScale;                 // xyz = position scale, w = trash
    vec4 posTranslation;           // xyz = position translation, w = trash
    vec4 uvScaleTranslation;       // xy = UV Scale, zw = UV offset
};

// Cluster-level uniforms array (bound once, indexed per draw)
layout(std430, set = 1, binding = 0) buffer ClusterBuffer
{
    ClusterData cluster[];  // Unsized array of structs
};

// Push constant for cluster index (updated per draw)
layout(push_constant) uniform PushConstants
{
    uint clusterIndex;  // Index into cluster array
} push;

// Vertex inputs
layout(location = 0) in ivec4 in_PosExtra;      // xyz = compressed pos (R16G16B16_SINT), w = extra 16 bits
layout(location = 1) in uvec2 in_UV;            // compressed UV (R16G16_UNORM)
layout(location = 2) in uvec4 in_OctNormTan;    // high 8 bits of oct normal/tangent (R8G8B8A8_UINT)

// Fragment shader inputs
layout(location = 0) out struct _o
{
    mat3 T2w;             // Tangent to small-world (rotation only)
    vec4 wSpacePosition;  // Position in small world
    vec4 ShadowPosition;  // Position in shadow texture-clip-space
    vec2 UV;              // Final texture coordinates

    vec3 Normal;
} Out;

void main()
{
    // Select cluster by push constant index
    ClusterData selectedCluster = cluster[push.clusterIndex];

    // Decode compressed position from int16 to [-1,1]
    const vec3 norm_pos     = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    const vec4 local_pos    = vec4(norm_pos * selectedCluster.posScale.xyz + selectedCluster.posTranslation.xyz, 1.0);

    // Transform to all needed spaces
    Out.wSpacePosition      = mesh.L2w       * local_pos;
    Out.ShadowPosition      = mesh.L2CShadow * local_pos;
    gl_Position             = mesh.w2C       * Out.wSpacePosition;

    // Decode UV and apply cluster scaling/offset
    const vec2 norm_uv      = vec2(in_UV) / 65535.0;
    Out.UV                  = norm_uv * selectedCluster.uvScaleTranslation.xy + selectedCluster.uvScaleTranslation.zw;

    // Extract extra_bits
    const uint extra_bits     = uint(in_PosExtra.w);

    // Binormal sign: bit 15 of extra_bits (0:+1, 1:-1)
    const uint sign_bit       = (extra_bits >> 15u) & 1u;
    const float binormal_sign = (sign_bit == 0u) ? 1.0 : -1.0;

    // Remaining 15 bits (0-14) for low-precision components
    // N_x_low: 4 bits (bits 0-3)
    // N_y_low: 4 bits (bits 4-7)
    // T_x_low: 4 bits (bits 8-11)
    // T_y_low: 3 bits (bits 12-14)
    const uvec4 lows        = (uvec4(extra_bits) >> uvec4(0u,4u,8u,12u)) & uvec4(0xFu,0xFu,0xFu,7u);

    // Combine high 8 bits (from vertex attr) + low bits
    const uvec4 combined    = (in_OctNormTan << uvec4(4u,4u,4u,3u)) | lows;

    // Decode oct-encoded normal (12 bit) and tangent (12/11 bit)
    const vec2 enc_normal   = vec2(combined.xy) / 4095.0;
    const vec2 enc_tangent  = vec2(combined.z / 4095.0, combined.w / 2047.0);

    const vec3 Normal       = oct_decode(enc_normal);
    const vec3 Tangent      = oct_decode(enc_tangent);
    const vec3 Binormal     = cross(Normal, Tangent) * binormal_sign;

    // Build rotation-only matrix from L2w (strips scaling/shear)
    const mat3 L2w_rot      = mat3( normalize(mesh.L2w[0].xyz),
                                    normalize(mesh.L2w[1].xyz),
                                    normalize(mesh.L2w[2].xyz));

    // Tangent-to-world matrix (rotation only)
    Out.T2w = L2w_rot * mat3(Tangent, Binormal, Normal);

    Out.Normal = Normal;
}


/**** Old Push constant version
#version 450

// Optimized Oct decode function
vec3 oct_decode(vec2 e)
{
    // Remap to [-1, 1] range
    e = e * 2.0 - 1.0;

    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
    {
        v.x = (1.0 - abs(v.y)) * sign(v.x);
        v.y = (1.0 - abs(v.x)) * sign(v.y);
    }
    return normalize(v);
}

// Push constants for per-cluster data
layout(push_constant) uniform PushConsts
{
    vec4 posScaleAndUScale;             // .xyz = position scale, .w = U scale
    vec4 posTranslationAndVScale;       // .xyz = position translation, .w = V scale
    vec2 uvTranslation;                 // .xy = UV translation
    mat4 L2w;                           // Local to small world (camera-centered)
    mat4 w2C;                           // Small world to clip space
    mat4 L2CShadow;                     // Shadow space matrix
    //vec4 LightColor;
    //vec4 AmbientLightColor;
    //vec4 wSpaceLightPos;
    //vec4 wSpaceEyePos;

} push;

// Input attributes
layout(location = 0) in ivec4 in_PosExtra;      // R16G16B16_SINT + R16_UINT (extra_bits)
layout(location = 1) in uvec2 in_UV;            // R16G16_UNORM
layout(location = 2) in uvec4 in_OctNormTan;    // R8G8B8A8_UINT (octN.xy + octT.xy high bits)

// Output varyings
layout(location = 0) out struct _o
{
    mat3 T2w;                           // Tangent space to w/small world (Rotation Only)
    vec4 wSpacePosition;                // Vertex pos in w/"small world"
    vec4 ShadowPosition;                // Vertex position in shadow space
    vec2 UV;                            // Just your regular UVs
} Out;

void main()
{
    // Decode position: int16 [-32768,32767] -> norm [-1,1]
    vec3    norm_pos = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    vec4    local_pos = vec4(norm_pos.xyz * push.posScaleAndUScale.xyz + push.posTranslationAndVScale.xyz, 1.0);
    Out.wSpacePosition = push.L2w * local_pos;
    Out.ShadowPosition = push.L2CShadow * local_pos;
    gl_Position = push.w2C * Out.wSpacePosition;

    // Decode normalized UV: uint16 [0,65535] -> [0,1] (intermediate)
    // Final UV can be outside [0,1] or negative for wrapping/tiling
    vec2    norm_uv = vec2(in_UV) / 65535.0;
    Out.UV = norm_uv * vec2(push.posScaleAndUScale.w, push.posTranslationAndVScale.w) + push.uvTranslation;

    // Extract extra_bits
    uint    extra_bits = uint(in_PosExtra.w); // 16 bits total

    // Binormal sign: bit 15 of extra_bits (0:+1, 1:-1)
    uint    sign_bit = ((extra_bits >> 15u) & 1u);
    float   binormal_sign = (sign_bit == 0u) ? 1.0 : -1.0;

    // Remaining 15 bits (0-14) for low-precision components
    // N_x_low: 4 bits (bits 0-3)
    // N_y_low: 4 bits (bits 4-7)
    // T_x_low: 4 bits (bits 8-11)
    // T_y_low: 3 bits (bits 12-14)
    uvec4   lows = (uvec4(extra_bits) >> uvec4(0u, 4u, 8u, 12u)) & uvec4(0xFu, 0xFu, 0xFu, 7u);

    // Combine high (from in_OctNormTan) and low (from extra_bits)
    // N_x, N_y, T_x: 8 (high) + 4 (low) = 12 bits (0-4095)
    // T_y: 8 (high) + 3 (low) = 11 bits (0-2047)
    uvec4   combined = (in_OctNormTan << uvec4(4u, 4u, 4u, 3u)) | lows;

    // Normalize to [0,1] for oct_decode
    vec2    enc_normal = vec2(combined.xy) / 4095.0;
    vec2    enc_tangent = vec2(combined.z / 4095.0, combined.w / 2047.0);
    vec3    Normal = oct_decode(enc_normal);
    vec3    Tangent = oct_decode(enc_tangent);
    vec3    Binormal = cross(Normal, Tangent) * binormal_sign;

    // Optional: Gram-Schmidt to orthonormalize T
    // Tangent = normalize(Tangent - Normal * dot(Normal, Tangent));

    // Extract the rotation and avoid scaling issues
    mat3    L2w_rot = mat3(normalize(push.L2w[0].xyz),
        normalize(push.L2w[1].xyz),
        normalize(push.L2w[2].xyz));
    Out.T2w = L2w_rot * mat3(Tangent, Binormal, Normal);
}
****/


/*****************************************************
// Higher precision... 16 bit for T, N (from verts)

#version 450

// Oct decode function
vec3 oct_decode(vec2 e) 
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * mix(vec2(1.0), vec2(-1.0), step(v.xy, vec2(0.0)));
    return normalize(v);
}

// Push constants for per-cluster data
layout(push_constant) uniform PushConsts
{
    vec4 posScaleAndUScale;         // .xyz = position scale, .w = U scale
    vec4 posTranslationAndVScale;   // .xyz = position translation, .w = V scale
    vec2 uvTranslation;             // .xy = UV translation
    mat4 L2w;                       // Local to small world (camera-centered)
    mat4 w2C;                       // Small world to clip space
    vec4 LightColor;
    vec4 AmbientLightColor;
    vec4 wSpaceLightPos;
    vec4 wSpaceEyePos;
} push;

// Input attributes
layout(location = 0) in  ivec4 in_PosExtra;     // R16G16B16A16_SINT
layout(location = 1) in  uvec2 in_UV;           // R16G16_UNORM
layout(location = 2) in  ivec4 in_OctNormTan;   // R16G16B16A16_SNORM

// Output varyings
layout(location = 0) out struct _o
{
    mat3  T2w;                          // Tangent space to w/small world (Rotation Only)
    vec4  wSpacePosition;               // Vertex pos in w/"small world"
    vec2  UV;                           // Just your regular UVs
} Out;

void main() 
{
    // Decode position: int16 [-32768,32767] -> norm [-1,1]
    vec3 norm_pos           = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    vec3 local_pos          = norm_pos * push.posScaleAndUScale.xyz + push.posTranslationAndVScale.xyz;
    Out.wSpacePosition      = push.L2w * vec4(local_pos, 1.0);
    gl_Position             = push.w2C * Out.wSpacePosition;

    // Decode normalized UV: uint16 [0,65535] -> [0,1] (intermediate)
    // Final UV can be outside [0,1] or negative for wrapping/tiling
    vec2 norm_uv            = vec2(in_UV) / 65535.0;
    Out.UV                  = norm_uv * vec2(push.posScaleAndUScale.w, push.posTranslationAndVScale.w) + push.uvTranslation;

    // Decode oct-encoded normal/tangent: int16 [-32767,32767] -> [-1,1]
    vec2 enc_n              = vec2(in_OctNormTan.xy) / 32767.0;
    vec3 Normal             = oct_decode(enc_n);

    vec2 enc_t              = vec2(in_OctNormTan.zw) / 32767.0;
    vec3 Tangent            = oct_decode(enc_t);

    // Binormal sign: bit 0 of extra (0:+1, 1:-1)
    float binormal_sign     = (in_PosExtra.w & 1) == 0 ? 1.0 : -1.0;
    vec3 Binormal           = cross(Normal, Tangent) * binormal_sign;

    // Optional: Gram-Schmidt to orthonormalize T
    // Tangent = normalize(Tangent - Normal * dot(Normal, Tangent));

    // Extra the rotationing and avoid scaling issues
    mat3    L2w_rot         = mat3(normalize(push.L2w[0].xyz),
                                   normalize(push.L2w[1].xyz),
                                   normalize(push.L2w[2].xyz));
    Out.T2w                 = mat3(L2w_rot) * mat3(Tangent, Binormal, Normal);
}

***********************************************************/