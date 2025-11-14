#version 450

// Push constants for per-cluster data
layout(push_constant) uniform PushConsts
{
    vec4 posScaleAndUScale;             // .xyz = position scale, .w = U scale
    vec4 posTranslationAndVScale;       // .xyz = position translation, .w = V scale
    mat4 L2C;                           // Local to small world (camera-centered)
} push;

// Input attributes
layout(location = 0) in ivec4 in_PosExtra;      // R16G16B16_SINT + R16_UINT (extra_bits)

void main()
{
    // Decode position: int16 [-32768,32767] -> norm [-1,1]
    vec3    norm_pos            = (vec3(in_PosExtra.xyz) + 32768.0) / 32767.5 - 1.0;
    vec4    local_pos           = vec4( norm_pos.xyz * push.posScaleAndUScale.xyz + push.posTranslationAndVScale.xyz, 1.0);
    gl_Position                 = push.L2C       * local_pos;
}

