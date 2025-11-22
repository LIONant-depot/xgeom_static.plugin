#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(binding = 0) uniform sampler2D SamplerShadowMap;        // [INPUT_TEXTURE_SHADOW]
layout(binding = 1) uniform sampler2D SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]
 
//layout(binding = 0)    uniform     sampler2D   SamplerNormalMap;		// [INPUT_TEXTURE_NORMAL]
//layout(binding = 1)    uniform     sampler2D   SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]
//layout(binding = 2)    uniform     sampler2D   SamplerAOMap;			// [INPUT_TEXTURE_AO]
//layout(binding = 3)    uniform     sampler2D   SamplerGlossivessMap;	// [INPUT_TEXTURE_GLOSSINESS]
//layout(binding = 4)    uniform     sampler2D   SamplerRoughnessMap;		// [INPUT_TEXTURE_ROUGHNESS]

layout(location = 0) in struct
{
	mat3 T2w;                          // Tangent space to w/small world (Rotation Only)
	vec4 wSpacePosition;               // Vertex pos in w/"small world"
	vec4 ShadowPosition;               // Vertex position in shadow space
	vec2 UV;                           // Just your regular UVs
} In;

layout(set = 2, binding = 0) uniform MeshUniforms
{
	mat4 L2w;          // Local space -> camera-centered small world
	mat4 w2C;          // Small world -> clip space (projection * view)
	mat4 L2CShadow;    // Local space -> shadow clip space

	vec4 LightColor;
	vec4 AmbientLightColor;
	vec4 wSpaceLightPos;
	vec4 wSpaceEyePos;
} mesh;

layout(location = 0)   out         vec4        outFragColor;

//----------------------------------------------------------------------------------------

int isqr(int a) { return a * a; }

//----------------------------------------------------------------------------------------

float SampleShadowTexture(in const vec4 Coord, in const vec2 off)
{
	float dist = texture(SamplerShadowMap, Coord.xy + off).r;
	return (Coord.w > 0.0 && dist < Coord.z) ? 0.0f : 1.0f;
}

//----------------------------------------------------------------------------------------

float ShadowPCF(in const vec4 UVProjection)
{
	float Shadow = 1.0;
	if (UVProjection.z > -1.0 && UVProjection.z < 1.0)
	{
		const float scale		= 1.5;
		const vec2  TexelSize	= scale / textureSize(SamplerShadowMap, 0);
		const int	SampleRange = 1;
		const int	SampleTotal = isqr(1 + 2 * SampleRange);

		float		ShadowAcc	= 0;
		for (int x = -SampleRange; x <= SampleRange; x++)
		{
			for (int y = -SampleRange; y <= SampleRange; y++)
			{
				ShadowAcc += SampleShadowTexture(UVProjection, vec2(TexelSize.x * x, TexelSize.y * y));
			}
		}

		Shadow = ShadowAcc / SampleTotal;
	}

	return Shadow;
}

vec3 vec2col(vec3 V)
{
	return V*0.5 + 0.5;
}

//----------------------------------------------------------------------------------------

void main()
{
	const vec4  DiffuseColor = texture(SamplerDiffuseMap, In.UV);
	const float Shadow       = ShadowPCF(In.ShadowPosition / In.ShadowPosition.w);

	vec3 wNormal	= normalize(In.T2w * vec3(0,0,1));
	vec3 wLightDir	= normalize(mesh.wSpaceLightPos.xyz - In.wSpacePosition.xyz);
	float I = max(0,dot(wNormal, wLightDir));

	const vec3 FinalColor = (I * mesh.LightColor.rgb * Shadow + mesh.AmbientLightColor.rgb)* DiffuseColor.rgb;  //vec2col(wNormal);

	// Convert to gamma
	const float Gamma = 2.2; //pushConsts.wSpaceEyePos.w;
	outFragColor.a = 1;
	outFragColor.rgb = pow(FinalColor.rgb, vec3(1.0f / Gamma));



/*
	//
	// get the normal from a compress texture BC5
	//
	vec3 Normal;

	Normal = (texture(SamplerNormalMap, In.UV).xyz * 2.0) - 1.0;

	// Transform the normal to world space 
	Normal = normalize(In.BTN * Normal);

	//
	// do Lighting
	//

	// Note that the real light direction is the negative of this, but the negative is removed to speed up the equations
	vec3 LightDirection = normalize(pushConsts.WorldSpaceLightPos.xyz - In.WorldSpacePosition.xyz);

	// Compute the diffuse intensity
	float DiffuseI = max(0, dot(Normal, LightDirection));

	// Note This is the true Eye to Texel direction 
	vec3 EyeDirection = normalize(In.WorldSpacePosition.xyz - pushConsts.WorldSpaceEyePos.xyz);

	// Determine the power for the specular based on how rough something is
	const float Shininess = mix(1, 100, 1 - texture(SamplerRoughnessMap, In.UV).r);

	// The old way to Compute Specular "PHONG"
	float SpecularI1 = pow(max(0, dot(reflect(LightDirection, Normal), EyeDirection)), Shininess);

	// Read the diffuse color
	vec4 DiffuseColor = texture(SamplerDiffuseMap, In.UV) * In.VertColor;

	// Set the global constribution
	outFragColor.rgb = pushConsts.AmbientLightColor.rgb * DiffuseColor.rgb * texture(SamplerAOMap, In.UV).rgb;

	// Add the contribution of this light
	outFragColor.rgb += pushConsts.LightColor.rgb * (SpecularI1.rrr * texture(SamplerGlossivessMap, In.UV).rgb + DiffuseI.rrr * DiffuseColor.rgb);

	// Convert to gamma
	const float Gamma = pushConsts.WorldSpaceEyePos.w;
	outFragColor.a = DiffuseColor.a;
	outFragColor.rgb = pow(outFragColor.rgb, vec3(1.0f / Gamma));

	*/
}


