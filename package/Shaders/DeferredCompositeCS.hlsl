
#include "Common/Color.hlsl"
#include "Common/DeferredShared.hlsli"
#include "Common/FrameBuffer.hlsl"
#include "Common/GBuffer.hlsli"
#include "Common/VR.hlsli"

Texture2D<half3> SpecularTexture : register(t0);
Texture2D<unorm half3> AlbedoTexture : register(t1);
Texture2D<unorm half3> NormalRoughnessTexture : register(t2);
Texture2D<unorm half3> MasksTexture : register(t3);
Texture2D<unorm half3> Masks2Texture : register(t4);

RWTexture2D<half3> MainRW : register(u0);
RWTexture2D<half4> NormalTAAMaskSpecularMaskRW : register(u1);
RWTexture2D<half2> SnowParametersRW : register(u2);

#if defined(DYNAMIC_CUBEMAPS)
Texture2D<float> DepthTexture : register(t5);
Texture2D<half3> ReflectanceTexture : register(t6);
TextureCube<half3> EnvTexture : register(t7);
TextureCube<half3> EnvReflectionsTexture : register(t8);

SamplerState LinearSampler : register(s0);
#endif

#if defined(SKYLIGHTING)
#	define SL_INCL_STRUCT
#	define SL_INCL_METHODS
#	include "Skylighting/Skylighting.hlsli"

cbuffer SkylightingCB : register(b1)
{
	SkylightingSettings skylightingSettings;
};

Texture3D<sh2> SkylightingProbeArray : register(t9);
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	half2 uv = half2(dispatchID.xy + 0.5) * BufferDim.zw;
	uint eyeIndex = GetEyeIndexFromTexCoord(uv);
	uv = ConvertFromStereoUV(uv, eyeIndex);

	half3 normalGlossiness = NormalRoughnessTexture[dispatchID.xy];
	half3 normalVS = DecodeNormal(normalGlossiness.xy);

	half3 diffuseColor = MainRW[dispatchID.xy];
	half3 specularColor = SpecularTexture[dispatchID.xy];
	half3 albedo = AlbedoTexture[dispatchID.xy];
	half3 masks2 = Masks2Texture[dispatchID.xy];

	half2 snowParameters = masks2.xy;
	half pbrWeight = masks2.z;

	half glossiness = normalGlossiness.z;

	half3 color = lerp(diffuseColor + specularColor, Lin2sRGB(sRGB2Lin(diffuseColor) + sRGB2Lin(specularColor)), pbrWeight);

#if defined(DYNAMIC_CUBEMAPS)

	half3 reflectance = ReflectanceTexture[dispatchID.xy];

	if (reflectance.x > 0.0 || reflectance.y > 0.0 || reflectance.z > 0.0) {
		half3 normalWS = normalize(mul(CameraViewInverse[eyeIndex], half4(normalVS, 0)).xyz);

		half wetnessMask = MasksTexture[dispatchID.xy].z;

		normalWS = lerp(normalWS, float3(0, 0, 1), wetnessMask);

		color = sRGB2Lin(color);

		half depth = DepthTexture[dispatchID.xy];

		half4 positionCS = half4(2 * half2(uv.x, -uv.y + 1) - 1, depth, 1);
		positionCS = mul(CameraViewProjInverse[eyeIndex], positionCS);
		positionCS.xyz = positionCS.xyz / positionCS.w;

		half3 positionWS = positionCS.xyz;

		half3 V = normalize(positionWS);
		half3 R = reflect(V, normalWS);

		half roughness = 1.0 - glossiness;
		half level = roughness * 7.0;
	

		half3 directionalAmbientColor = sRGB2Lin(mul(DirectionalAmbient, half4(R, 1.0)));
		half3 finalIrradiance = lerp(0, directionalAmbientColor, pbrWeight);

#	if defined(INTERIOR)
		half3 specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level).xyz;
		specularIrradiance = sRGB2Lin(specularIrradiance);

		finalIrradiance += specularIrradiance;
#	elif defined(SKYLIGHTING)
#		if defined(VR)
		float3 positionMS = positionWS + (eyeIndex == 1 ? CameraPosAdjust[1] - CameraPosAdjust[0] : 0);
#		else
		float3 positionMS = positionWS;
#		endif

		sh2 skylighting = sampleSkylighting(skylightingSettings, SkylightingProbeArray, positionWS.xyz, normalWS);
		sh2 specularLobe = fauxSpecularLobeSH(normalWS, -V, roughness);

		half skylightingSpecular = saturate(shFuncProductIntegral(skylighting, specularLobe));
		skylightingSpecular = lerp(skylightingSettings.MixParams.z, 1, saturate(skylightingSpecular * skylightingSettings.MixParams.w));
		skylightingSpecular = applySkylightingFadeout(skylightingSpecular, length(positionWS.xyz));

		half3 specularIrradiance = 1;

		if (skylightingSpecular < 1.0) {
			specularIrradiance = EnvTexture.SampleLevel(LinearSampler, R, level).xyz;
			specularIrradiance = sRGB2Lin(specularIrradiance);
		}

		half3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0) {
			specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level).xyz;
			specularIrradianceReflections = sRGB2Lin(specularIrradianceReflections);
		}
		finalIrradiance += lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#	else
		half3 specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(LinearSampler, R, level).xyz;
		specularIrradianceReflections = sRGB2Lin(specularIrradianceReflections);

		finalIrradiance += specularIrradianceReflections;
#	endif

		color += reflectance * finalIrradiance;

		color = Lin2sRGB(color);
	}

#endif

#if defined(DEBUG)

#	if defined(VR)
	uv.x += (eyeIndex ? 0.1 : -0.1);
#	endif  // VR

	if (uv.x < 0.5 && uv.y < 0.5) {
		color = color;
	} else if (uv.x < 0.5) {
		color = albedo;
	} else if (uv.y < 0.5) {
		color = normalVS;
	} else {
		color = glossiness;
	}

#endif

	MainRW[dispatchID.xy] = min(color, 128);  // Vanilla bloom fix
	NormalTAAMaskSpecularMaskRW[dispatchID.xy] = half4(EncodeNormalVanilla(normalVS), 0.0, 0.0);
	SnowParametersRW[dispatchID.xy] = snowParameters;
}