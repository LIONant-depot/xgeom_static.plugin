#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "mb_standard_pbr_frag.glsl"

layout(binding = 1) uniform sampler2D SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]

layout(location = 0) out vec4 outFragColor;

void main()
{
	vec3 FinalColor = PBRLighting
	( vec3(0, 0, 1)
	, texture(SamplerDiffuseMap, In.UV).rgb
	, 1
	, vec3(0.04)
	, 0.8
	, 0
	, 1
	);

	//
	// Tone map from (HDR) to (LDR) before gamma correction
	// Has a blue tint 
	//
	FinalColor.rgb = FinalColor.rgb / (FinalColor.rgb + vec3(1));//vec3(1.0, 1.0, 0.9));

	// Convert to gamma
	const float Gamma = 2.2; //pushConsts.wSpaceEyePos.w;
	outFragColor.a = 1;
	outFragColor.rgb = pow(FinalColor.rgb, vec3(1.0f / Gamma));
}

