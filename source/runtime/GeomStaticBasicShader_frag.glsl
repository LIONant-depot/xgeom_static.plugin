#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "mb_standard_shadow_frag.glsl"

layout(binding = 1) uniform sampler2D SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]

layout(location = 0) in struct
{
	mat3 T2w;                          // Tangent space to w/small world (Rotation Only)
	vec4 wSpacePosition;               // Vertex pos in w/"small world"
	vec4 ShadowPosition;               // Vertex position in shadow space
	vec2 UV;                           // Just your regular UVs
} In;

layout(set = 2, binding = 1) uniform lighting_uniforms
{
	vec4 LightColor;
	vec4 AmbientLightColor;
	vec4 wSpaceLightPos;
	vec4 wSpaceEyePos;
} LightingUniforns;

layout(location = 0) out vec4 outFragColor;


void main()
{
	const vec4  DiffuseColor = texture(SamplerDiffuseMap, In.UV);
	const float Shadow = ShadowPCF(In.ShadowPosition / In.ShadowPosition.w);

	vec3 wNormal = normalize(In.T2w * vec3(0, 0, 1));
	vec3 wLightDir = normalize(LightingUniforns.wSpaceLightPos.xyz - In.wSpacePosition.xyz);
	float I = max(0, dot(wNormal, wLightDir));

	const vec3 FinalColor = (I * LightingUniforns.LightColor.rgb * Shadow + LightingUniforns.AmbientLightColor.rgb) * DiffuseColor.rgb;  //vec2col(wNormal);

	// Convert to gamma
	const float Gamma = 2.2; //pushConsts.wSpaceEyePos.w;
	outFragColor.a = 1;
	outFragColor.rgb = pow(FinalColor.rgb, vec3(1.0f / Gamma));
}

