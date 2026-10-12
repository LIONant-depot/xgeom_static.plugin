#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// What the static geom preview draws a submesh with when its material does not resolve: runtime/GeomStaticBasicShader_frag.glsl
// without the tone map - the preview draws through xLIONRender, which writes scene-linear HDR and tone maps after.
// (GeomStaticBasicShader_frag.glsl stays as is: the xGPU examples E21/E25 draw it straight to the screen.)
#include "../../runtime/mb_standard_pbr.frag"

layout(binding = 1) uniform sampler2D SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]

layout(location = 0) out vec4 outFragColor;

void main()
{
	vec3 FinalColor = PBRLighting
	( vec3(0, 0, 1)
	, texture(SamplerDiffuseMap, In.UV).rgb
	, 1
	, 0.04
	, 1
	, 0
	, vec3(0)
	);
	outFragColor = vec4(FinalColor, 1.0);
}
