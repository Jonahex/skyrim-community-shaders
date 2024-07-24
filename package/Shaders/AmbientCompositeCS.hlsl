#include "Common/Color.hlsl"
#include "Common/DeferredShared.hlsli"
#include "Common/FrameBuffer.hlsl"
#include "Common/GBuffer.hlsli"
#include "Common/VR.hlsli"

Texture2D<unorm half3> AlbedoTexture : register(t0);
Texture2D<unorm half3> NormalRoughnessTexture : register(t1);

#if defined(SKYLIGHTING)
#	define SL_INCL_STRUCT
#	define SL_INCL_METHODS
#	include "Skylighting/Skylighting.hlsli"

cbuffer SkylightingCB : register(b1)
{
	SkylightingSettings skylightingSettings;
};

Texture2D<unorm float> DepthTexture : register(t2);
Texture3D<sh2> SkylightingProbeArray : register(t3);
#endif

#if defined(SSGI)
Texture2D<half4> SSGITexture : register(t4);
#endif

Texture2D<unorm half3> Masks2Texture : register(t5);

RWTexture2D<half3> MainRW : register(u0);
#if defined(SSGI)
RWTexture2D<half3> DiffuseAmbientRW : register(u1);
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	half2 uv = half2(dispatchID.xy + 0.5) * BufferDim.zw;
	uint eyeIndex = GetEyeIndexFromTexCoord(uv);

	half3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	half3 normalVS = DecodeNormal(normalGlossiness.xy);

	half3 diffuseColor = MainRW[dispatchID.xy];
	half3 albedo = AlbedoTexture[dispatchID.xy];
	half3 masks2 = Masks2Texture[dispatchID.xy];

	half pbrWeight = masks2.z;

	half3 normalWS = normalize(mul(CameraViewInverse[eyeIndex], half4(normalVS, 0)).xyz);

	half3 directionalAmbientColor = mul(DirectionalAmbient, half4(normalWS, 1.0));

	half3 linAlbedo = sRGB2Lin(albedo);
	half3 linDirectionalAmbientColor = sRGB2Lin(directionalAmbientColor);
	half3 linDiffuseColor = sRGB2Lin(diffuseColor);

	half3 linAmbient = lerp(sRGB2Lin(albedo * directionalAmbientColor), linAlbedo * linDirectionalAmbientColor, pbrWeight);
	linAmbient = max(0, linAmbient);  // Fixes black blobs on the world map

	half visibility = 1.0;
#if defined(SKYLIGHTING)
	float rawDepth = DepthTexture[dispatchID.xy];
	float4 positionCS = float4(2 * float2(uv.x, -uv.y + 1) - 1, rawDepth, 1);
	float4 positionMS = mul(CameraViewProjInverse[eyeIndex], positionCS);
	positionMS.xyz = positionMS.xyz / positionMS.w;
#	if defined(VR)
	if (eyeIndex == 1)
		positionMS.xyz += CameraPosAdjust[1] - CameraPosAdjust[0];
#	endif

	sh2 skylighting = sampleSkylighting(skylightingSettings, SkylightingProbeArray, positionMS.xyz, normalWS);
	half skylightingDiffuse = shHallucinateZH3Irradiance(skylighting, skylightingSettings.DirectionalDiffuse ? normalWS : float3(0, 0, 1));
	skylightingDiffuse = lerp(skylightingSettings.MixParams.x, 1, saturate(skylightingDiffuse * skylightingSettings.MixParams.y));
	skylightingDiffuse = applySkylightingFadeout(skylightingDiffuse, length(positionMS.xyz));

	visibility = skylightingDiffuse;
#endif

#if defined(SSGI)
	half4 ssgiDiffuse = SSGITexture[dispatchID.xy];
	ssgiDiffuse.rgb *= linAlbedo;

	visibility = min(visibility, ssgiDiffuse.a);

	DiffuseAmbientRW[dispatchID.xy] = linAmbient + ssgiDiffuse.rgb;

#	if defined(INTERIOR)
	linDiffuseColor *= ssgiDiffuse.a;
#	endif
	linDiffuseColor += ssgiDiffuse.rgb;
#endif

	linAmbient *= visibility;

	half3 ambient = Lin2sRGB(linAmbient);
	diffuseColor = Lin2sRGB(linDiffuseColor);

	diffuseColor = lerp(diffuseColor + ambient, Lin2sRGB(linDiffuseColor + linAmbient), pbrWeight);

	MainRW[dispatchID.xy] = diffuseColor;
};