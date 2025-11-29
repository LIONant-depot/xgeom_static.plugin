#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "Cache/Plugins/xgeom_static/Runtime/mb_standard_pbr_frag.glsl"

layout(binding = 0) uniform sampler2D SamplerShadowMap;        // [INPUT_TEXTURE_100] 
layout(binding = 1) uniform sampler2D SamplerNormal;           // [INPUT_TEXTURE_110]
layout(binding = 2) uniform sampler2D SamplerAlbedo;           // [INPUT_TEXTURE_120]
layout(binding = 3) uniform sampler2D SamplerAO;               // [INPUT_TEXTURE_130]
layout(binding = 4) uniform sampler2D SamplerRoughness;        // [INPUT_TEXTURE_140]
layout(binding = 5) uniform sampler2D SamplerMetalness;        // [INPUT_TEXTURE_150]

layout(location = 0) out vec4 outFragColor;

void main()
{

}