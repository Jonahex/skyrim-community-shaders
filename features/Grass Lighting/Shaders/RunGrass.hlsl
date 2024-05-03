#include "Common/Color.hlsl"
#include "Common/FrameBuffer.hlsl"
#include "Common/GBuffer.hlsli"
#include "Common/LightingData.hlsl"
#include "Common/MotionBlur.hlsl"

#define GRASS

struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 TexCoord : TEXCOORD0;
	float4 Normal : NORMAL0;
	float4 Color : COLOR0;
	float4 InstanceData1 : TEXCOORD4;
	float4 InstanceData2 : TEXCOORD5;
	float4 InstanceData3 : TEXCOORD6;
	float4 InstanceData4 : TEXCOORD7;
#ifdef VR
	uint InstanceID : SV_INSTANCEID;
#endif  // VR
};

struct VS_OUTPUT
{
	float4 HPosition : SV_POSITION0;
	float4 VertexColor : COLOR0;
	float3 TexCoord : TEXCOORD0;
	float3 ViewSpacePosition :
#if !defined(VR)
		TEXCOORD1;
#else
		TEXCOORD2;
#endif  // !VR
#if defined(RENDER_DEPTH)
	float2 Depth :
#	if !defined(VR)
		TEXCOORD2;
#	else
		TEXCOORD3;
#	endif  // !VR
#endif      // RENDER_DEPTH
	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
	float3 VertexNormal : POSITION4;
	float4 SphereNormal : POSITION5;
#ifdef VR
	float ClipDistance : SV_ClipDistance0;
	float CullDistance : SV_CullDistance0;
#endif  // !VR
};

// Constant Buffers (Flat and VR)
cbuffer PerGeometry : register(
#ifdef VSHADER
						  b2
#else
						  b3
#endif
					  )
{
#if !defined(VR)
	row_major float4x4 WorldViewProj[1] : packoffset(c0);
	row_major float4x4 WorldView[1] : packoffset(c4);
	row_major float4x4 World[1] : packoffset(c8);
	row_major float4x4 PreviousWorld[1] : packoffset(c12);
	float4 FogNearColor : packoffset(c16);
	float3 WindVector : packoffset(c17);
	float WindTimer : packoffset(c17.w);
	float3 DirLightDirection : packoffset(c18);
	float PreviousWindTimer : packoffset(c18.w);
	float3 DirLightColor : packoffset(c19);
	float AlphaParam1 : packoffset(c19.w);
	float3 AmbientColor : packoffset(c20);
	float AlphaParam2 : packoffset(c20.w);
	float3 ScaleMask : packoffset(c21);
	float ShadowClampValue : packoffset(c21.w);
#else
	row_major float4x4 WorldViewProj[2] : packoffset(c0);
	row_major float4x4 WorldView[2] : packoffset(c8);
	row_major float4x4 World[2] : packoffset(c16);
	row_major float4x4 PreviousWorld[2] : packoffset(c24);
	float4 FogNearColor : packoffset(c32);
	float3 WindVector : packoffset(c33);
	float WindTimer : packoffset(c33.w);
	float3 DirLightDirection : packoffset(c34);
	float PreviousWindTimer : packoffset(c34.w);
	float3 DirLightColor : packoffset(c35);
	float AlphaParam1 : packoffset(c35.w);
	float3 AmbientColor : packoffset(c36);
	float AlphaParam2 : packoffset(c36.w);
	float3 ScaleMask : packoffset(c37);
	float ShadowClampValue : packoffset(c37.w);
#endif  // !VR
}

cbuffer PerFrame : register(
#ifdef VSHADER
					   b3
#else
					   b4
#endif
				   )
{
	row_major float3x4 DirectionalAmbient;
	float SunlightScale;
	float Glossiness;
	float SpecularStrength;
	float SubsurfaceScatteringAmount;
	bool OverrideComplexGrassSettings;
	float BasicGrassBrightness;
}

#ifdef VSHADER

#	ifdef GRASS_COLLISION
#		include "GrassCollision\\GrassCollision.hlsli"
#	endif

cbuffer cb7 : register(b7)
{
	float4 cb7[1];
}

cbuffer cb8 : register(b8)
{
	float4 cb8[240];
}

float4 GetMSPosition(VS_INPUT input, float windTimer, float3x3 world3x3)
{
	float windAngle = 0.4 * ((input.InstanceData1.x + input.InstanceData1.y) * -0.0078125 + windTimer);
	float windAngleSin, windAngleCos;
	sincos(windAngle, windAngleSin, windAngleCos);

	float windTmp3 = 0.2 * cos(M_PI * windAngleCos);
	float windTmp1 = sin(M_PI * windAngleSin);
	float windTmp2 = sin(M_2PI * windAngleSin);
	float windPower = WindVector.z * (((windTmp1 + windTmp2) * 0.3 + windTmp3) *
										 (0.5 * (input.Color.w * input.Color.w)));

	float3 inputPosition = input.Position.xyz * (input.InstanceData4.yyy * ScaleMask.xyz + float3(1, 1, 1));
	float3 InstanceData4 = mul(world3x3, inputPosition);

	float3 windVector = float3(WindVector.xy, 0);

	float4 msPosition;
	msPosition.xyz = input.InstanceData1.xyz + (windVector * windPower + InstanceData4);
	msPosition.w = 1;

	return msPosition;
}

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	uint eyeIndex = GetEyeIndexVS(
#	if defined(VR)
		input.InstanceID
#	endif
	);
	float3x3 world3x3 = float3x3(input.InstanceData2.xyz, input.InstanceData3.xyz, float3(input.InstanceData4.x, input.InstanceData2.w, input.InstanceData3.w));

	float4 msPosition = GetMSPosition(input, WindTimer, world3x3);

#	ifdef GRASS_COLLISION
	float3 displacement = GetDisplacedPosition(msPosition.xyz, input.Color.w, eyeIndex);
	msPosition.xyz += displacement;
#	endif

	float4 projSpacePosition = mul(WorldViewProj[eyeIndex], msPosition);
#	if !defined(VR)
	vsout.HPosition = projSpacePosition;
#	endif  // !VR

#	if defined(RENDER_DEPTH)
	vsout.Depth = projSpacePosition.zw;
#	endif  // RENDER_DEPTH

#	ifdef VR
	float3 instanceNormal = float3(input.InstanceData2.z, input.InstanceData3.zw);
	float dirLightAngle = dot(DirLightDirection.xyz, instanceNormal);
	float3 diffuseMultiplier = input.InstanceData1.www * input.Color.xyz * saturate(dirLightAngle.xxx);
#	endif  // VR
	float perInstanceFade = dot(cb8[(asuint(cb7[0].x) >> 2)].xyzw, M_IdentityMatrix[(asint(cb7[0].x) & 3)].xyzw);
	float distanceFade = 1 - saturate((length(mul(WorldViewProj[0], msPosition).xyz) - AlphaParam1) / AlphaParam2);

	// Note: input.Color.w is used for wind speed
	vsout.VertexColor.xyz = input.Color.xyz * input.InstanceData1.www;
	vsout.VertexColor.w = distanceFade * perInstanceFade;

	vsout.TexCoord.xy = input.TexCoord.xy;
	vsout.TexCoord.z = FogNearColor.w;

	vsout.ViewSpacePosition = mul(WorldView[eyeIndex], msPosition).xyz;
	vsout.WorldPosition = mul(World[eyeIndex], msPosition);

	float4 previousMsPosition = GetMSPosition(input, PreviousWindTimer, world3x3);

#	ifdef GRASS_COLLISION
	previousMsPosition.xyz += displacement;
#	endif  // GRASS_COLLISION

	vsout.PreviousWorldPosition = mul(PreviousWorld[eyeIndex], previousMsPosition);
#	if defined(VR)
	VR_OUTPUT VRout = GetVRVSOutput(projSpacePosition, eyeIndex);
	vsout.HPosition = VRout.VRPosition;
	vsout.ClipDistance.x = VRout.ClipDistance;
	vsout.CullDistance.x = VRout.CullDistance;
#	endif  // !VR

	// Vertex normal needs to be transformed to world-space for lighting calculations.
	vsout.VertexNormal.xyz = mul(world3x3, input.Normal.xyz * 2.0 - 1.0);
	vsout.SphereNormal.xyz = mul(world3x3, normalize(input.Position.xyz));
	vsout.SphereNormal.w = saturate(input.Color.w);

	return vsout;
}
#endif  // VSHADER

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
#if defined(RENDER_DEPTH)
	float4 PS : SV_Target0;
#else
    float4 Diffuse : SV_Target0;
    float2 MotionVectors : SV_Target1;
    float4 NormalGlossiness : SV_Target2;
    float4 Albedo : SV_Target3;
    float4 Specular : SV_Target4;
    float4 Reflectance : SV_Target5;
    float4 Masks : SV_Target6;
#endif  // RENDER_DEPTH
};

#ifdef PSHADER
SamplerState SampBaseSampler : register(s0);
SamplerState SampShadowMaskSampler : register(s1);

#	if defined(TRUE_PBR)
SamplerState SampNormalSampler : register(s2);
SamplerState SampRMAOSSampler : register(s3);
SamplerState SampSubsurfaceSampler : register(s4);
#	endif

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexShadowMaskSampler : register(t1);

#	if defined(TRUE_PBR)
Texture2D<float4> TexNormalSampler : register(t2);
Texture2D<float4> TexRMAOSSampler : register(t3);
Texture2D<float4> TexSubsurfaceSampler : register(t4);
#	endif

cbuffer PerFrame : register(b0)
{
	float4 cb0_1[2] : packoffset(c0);
	float4 VPOSOffset : packoffset(c2);
	float4 cb0_2[7] : packoffset(c3);
}

#	if !defined(VR)
cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}
#	endif  // VR

#	if defined(TRUE_PBR)
cbuffer PerMaterial : register(b1)
{
    uint PBRFlags : packoffset(c0.x);
    float3 PBRParams1 : packoffset(c0.y); // roughness scale, specular level
    float4 PBRParams2 : packoffset(c1); // subsurface color, subsurface opacity
};
#	endif

float3 GetLightSpecularInput(float3 L, float3 V, float3 N, float3 lightColor, float shininess)
{
	float3 H = normalize(V + L);
	float HdotN = saturate(dot(H, N));

	float lightColorMultiplier = exp2(shininess * log2(HdotN));
	return lightColor * lightColorMultiplier.xxx;
}

float3 TransformNormal(float3 normal)
{
	return normal * 2 + -1.0.xxx;
}

// http://www.thetenthplanet.de/archives/1180
float3x3 CalculateTBN(float3 N, float3 p, float2 uv)
{
	// get edge vectors of the pixel triangle
	float3 dp1 = ddx_coarse(p);
	float3 dp2 = ddy_coarse(p);
	float2 duv1 = ddx_coarse(uv);
	float2 duv2 = ddy_coarse(uv);

	// solve the linear system
	float3 dp2perp = cross(dp2, N);
	float3 dp1perp = cross(N, dp1);
	float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	// construct a scale-invariant frame
	float invmax = rsqrt(max(dot(T, T), dot(B, B)));
	return float3x3(T * invmax, B * invmax, N);
}

#	if defined(LIGHT_LIMIT_FIX)
#		include "LightLimitFix/LightLimitFix.hlsli"
#	endif

#	define SampColorSampler SampBaseSampler
#	define PI 3.1415927

#	if defined(DYNAMIC_CUBEMAPS)
#		include "DynamicCubemaps/DynamicCubemaps.hlsli"
#	endif

#	if defined(TRUE_PBR)
#		include "Common/PBR.hlsli"
#	endif

PS_OUTPUT main(PS_INPUT input, bool frontFace
			   : SV_IsFrontFace)
{
	PS_OUTPUT psout;
	
#	if !defined(TRUE_PBR)
	float x;
	float y;
	TexBaseSampler.GetDimensions(x, y);
	bool complex = x != y;
#	endif

	float4 baseColor;
#	if !defined(TRUE_PBR)
	if (complex) {
		baseColor = TexBaseSampler.Sample(SampBaseSampler, float2(input.TexCoord.x, input.TexCoord.y * 0.5));
	} else 
#	endif
	{
		baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord.xy);
	}

#	if defined(RENDER_DEPTH) || defined(DO_ALPHA_TEST)
	float diffuseAlpha = input.VertexColor.w * baseColor.w;
	if ((diffuseAlpha - AlphaTestRefRS) < 0) {
		discard;
	}
#	endif  // RENDER_DEPTH | DO_ALPHA_TEST

#	if defined(RENDER_DEPTH)
	// Depth
	psout.PS.xyz = input.Depth.xxx / input.Depth.yyy;
	psout.PS.w = diffuseAlpha;
#	else
	
#		if !defined(TRUE_PBR)
	float4 specColor = complex ? TexBaseSampler.Sample(SampBaseSampler, float2(input.TexCoord.x, 0.5 + input.TexCoord.y * 0.5)) : 0;
#		else
    float4 specColor = TexNormalSampler.Sample(SampNormalSampler, input.TexCoord.xy);
#		endif
	float4 shadowColor = TexShadowMaskSampler.Load(int3(input.HPosition.xy, 0));

	uint eyeIndex = GetEyeIndexPS(input.HPosition, VPOSOffset);
	psout.MotionVectors = GetSSMotionVector(input.WorldPosition, input.PreviousWorldPosition, eyeIndex);

	float3 viewDirection = -normalize(input.WorldPosition.xyz);
	float3 normal = normalize(input.VertexNormal.xyz);

	// Swaps direction of the backfaces otherwise they seem to get lit from the wrong direction.
	if (!frontFace)
		normal = -normal;

	normal = normalize(lerp(normal, normalize(input.SphereNormal.xyz), input.SphereNormal.w));
	
#		if !defined(TRUE_PBR)
	if (complex) 
#		endif
	{
		float3 normalColor = TransformNormal(specColor.xyz);
		// world-space -> tangent-space -> world-space.
		// This is because we don't have pre-computed tangents.
		normal = normalize(mul(normalColor, CalculateTBN(normal, -input.WorldPosition.xyz, input.TexCoord.xy)));
	}
	
#		if !defined(TRUE_PBR)
	if (!complex || OverrideComplexGrassSettings)
		baseColor.xyz *= BasicGrassBrightness;
#		endif

	float3 dirLightColor = DirLightColor.xyz * SunlightScale;
	dirLightColor *= shadowColor.x;
	
#		if defined(TRUE_PBR)
    float4 rawRMAOS = TexRMAOSSampler.Sample(SampRMAOSSampler, input.TexCoord.xy) * float4(PBRParams1.x, 1, 1, PBRParams1.y);
	
    float roughness = 1;
    float metallic = 0;
    float ao = 1;
    float3 f0 = 0.04;
    float3 subsurfaceColor = 0;
    float thickness = 1;
	
    roughness = saturate(rawRMAOS.x);
    metallic = saturate(rawRMAOS.y);
    f0 = saturate(rawRMAOS.w);
	
    float3 pbrDiffuseColor = baseColor.xyz;
    f0 = lerp(f0, baseColor.xyz, metallic);
    baseColor.xyz *= 1 - metallic;
	
    ao = rawRMAOS.z;
	
    subsurfaceColor = PBRParams2.xyz;
    thickness = PBRParams2.w;
	[branch] if ((PBRFlags & TruePBR_HasSubsurface) != 0)
    {
        float4 sampledSubsurfaceProperties = TexSubsurfaceSampler.Sample(SampSubsurfaceSampler, input.TexCoord.xy);
        subsurfaceColor *= sampledSubsurfaceProperties.xyz;
        thickness *= sampledSubsurfaceProperties.w;
    }
	
    float3 transmissionColor = 0;
#		endif // TRUE_PBR

	float3 diffuseColor = 0;
	float3 specularColor = 0;

	float3 lightsDiffuseColor = 0;
	float3 lightsSpecularColor = 0;
	
#		if defined(TRUE_PBR)
	{
        float3 dirDiffuseColor, dirTransmissionColor, dirSpecularColor;
        GetDirectLightInputPBR(dirDiffuseColor, dirTransmissionColor, dirSpecularColor, normal, viewDirection, DirLightDirection, sRGB2Lin(dirLightColor), roughness, f0, subsurfaceColor, thickness);
        //lightsDiffuseColor += dirDiffuseColor;
        transmissionColor += dirTransmissionColor;
        lightsSpecularColor += dirSpecularColor;
    }
#		else
	float dirLightAngle = dot(normal, DirLightDirection.xyz);
	
	// Generated texture to simulate light transport.
	// Numerous attempts were made to use a more interesting algorithm however they were mostly fruitless.
	float3 subsurfaceColor = baseColor.xyz * sqrt(input.SphereNormal.w);

	// Applies lighting from the opposite direction. Does not account for normals perpendicular to the light source.
	lightsDiffuseColor += subsurfaceColor * dirLightColor * saturate(-dirLightAngle) * SubsurfaceScatteringAmount;

	if (complex)
		lightsSpecularColor += GetLightSpecularInput(DirLightDirection, viewDirection, normal, dirLightColor, Glossiness);
#		endif

#		if defined(LIGHT_LIMIT_FIX)
	float3 viewPosition = mul(CameraView[eyeIndex], float4(input.WorldPosition.xyz, 1)).xyz;
	float2 screenUV = ViewToUV(viewPosition, true, eyeIndex);

	uint clusterIndex = 0;
	uint lightCount = 0;

	if (GetClusterIndex(screenUV, viewPosition.z, clusterIndex)) {
		lightCount = lightGrid[clusterIndex].lightCount;
		if (lightCount) {
			uint lightOffset = lightGrid[clusterIndex].offset;

			float screenNoise = InterleavedGradientNoise(screenUV * lightingData[0].BufferDim);
			float shadowQualityScale = saturate(1.0 - (((float)lightCount * (float)lightCount) / 128.0));

			[loop] for (uint i = 0; i < lightCount; i++)
			{
				uint light_index = lightList[lightOffset + i];
				StructuredLight light = lights[light_index];

				float3 lightDirection = light.positionWS[eyeIndex].xyz - input.WorldPosition.xyz;
				float lightDist = length(lightDirection);
				float intensityFactor = saturate(lightDist / light.radius);
				if (intensityFactor == 1)
					continue;

				float intensityMultiplier = 1 - intensityFactor * intensityFactor;
				float3 lightColor = light.color.xyz;
				float3 normalizedLightDirection = normalize(lightDirection);

				float lightAngle = dot(normal, normalizedLightDirection);

				float3 normalizedLightDirectionVS = WorldToView(normalizedLightDirection, true, eyeIndex);
				if (light.firstPersonShadow)
					lightColor *= ContactShadows(viewPosition, screenUV, screenNoise, normalizedLightDirectionVS, shadowQualityScale, 0.0, eyeIndex);
				else if (perPassLLF[0].EnableContactShadows)
					lightColor *= ContactShadows(viewPosition, screenUV, screenNoise, normalizedLightDirectionVS, shadowQualityScale, 0.0, eyeIndex);
				
#			if defined(TRUE_PBR)
				{
                    float3 pointDiffuseColor, pointTransmissionColor, pointSpecularColor;
                    GetDirectLightInputPBR(pointDiffuseColor, pointTransmissionColor, pointSpecularColor, normal, viewDirection, normalizedLightDirection, sRGB2Lin(lightColor * intensityMultiplier), roughness, f0, subsurfaceColor, thickness);
                    lightsDiffuseColor += pointDiffuseColor;
                    transmissionColor += pointTransmissionColor;
                    lightsSpecularColor += pointSpecularColor;
                }
#			else
				float3 lightDiffuseColor = lightColor * saturate(lightAngle.xxx);

				lightDiffuseColor += subsurfaceColor * lightColor * saturate(-lightAngle) * SubsurfaceScatteringAmount;
				lightsDiffuseColor += lightDiffuseColor * intensityMultiplier;

				if (complex)
					lightsSpecularColor += GetLightSpecularInput(normalizedLightDirection, viewDirection, normal, lightColor, Glossiness) * intensityMultiplier;
#			endif
			}
		}
	}
#		endif

	diffuseColor += lightsDiffuseColor;

	float3 albedo = max(0, baseColor.xyz * input.VertexColor.xyz);
	diffuseColor *= albedo;
	
#		if defined(TRUE_PBR)
	specularColor += lightsSpecularColor;
	
    float3 diffuseIrradiance, specularIrradiance;
    GetAmbientLightInputPBR(diffuseIrradiance, specularIrradiance, normal, viewDirection, baseColor.xyz, roughness, f0, subsurfaceColor, thickness, ao);
    diffuseColor += diffuseIrradiance;
    specularColor += specularIrradiance;
    diffuseColor += transmissionColor;
#		else
	specularColor += lightsSpecularColor;
	specularColor *= specColor.w * SpecularStrength;
#		endif

#		if defined(LIGHT_LIMIT_FIX)
	if (perPassLLF[0].EnableLightsVisualisation) {
		if (perPassLLF[0].LightsVisualisationMode == 0) {
			diffuseColor.xyz = TurboColormap(0);
		} else if (perPassLLF[0].LightsVisualisationMode == 1) {
			diffuseColor.xyz = TurboColormap(0);
		} else {
			diffuseColor.xyz = TurboColormap((float)lightCount / 128.0);
		}
	} else {
		psout.Diffuse = float4(diffuseColor, 1);
	}
#		else
	psout.Diffuse.xyz = float4(diffuseColor, 1);
#		endif

	psout.Specular = float4(specularColor, 1);
	psout.Albedo = float4(albedo, 1);
	psout.Masks = float4(0, 0, 0, 0);

#	if defined(TRUE_PBR)
	psout.Reflectance.x = 1;
#	endif

	float3 normalVS = normalize(WorldToView(normal, false, eyeIndex));
	psout.NormalGlossiness = float4(EncodeNormal(normalVS), specColor.w, 1);
#	endif  // RENDER_DEPTH
	return psout;
}
#endif  // PSHADER
