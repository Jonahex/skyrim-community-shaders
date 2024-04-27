struct PBRData
{
    float DirectColorMultiplier;
    float DirectColorPower;
    float AmbientColorMultiplier;
    float AmbientColorPower;
	uint DiffuseModel;
	bool UseMultipleScattering;
	bool UseMultiBounceAO;
	bool UseDynamicCubemap;
	bool MatchDynamicCubemapToAmbient;
};

#define TruePBR_Lambert 0
#define TruePBR_Burley 1
#define TruePBR_OrenNayar 2
#define TruePBR_Gotanda 3
#define TruePBR_Chan 4

#define TruePBR_HasEmissive (1 << 0)
#define TruePBR_HasSubsurface (1 << 1)
#define TruePBR_Subsurface (1 << 3)
#define TruePBR_HasDisplacement (1 << 4)
	
StructuredBuffer<PBRData> pbrData : register(t121);

float3 AdjustDirectLightColorForPBR(float3 lightColor)
{
    return pbrData[0].DirectColorMultiplier * pow(sRGB2Lin(lightColor), pbrData[0].DirectColorPower);
}

float3 AdjustAmbientLightColorForPBR(float3 lightColor)
{
    return pbrData[0].AmbientColorMultiplier * pow(sRGB2Lin(lightColor), pbrData[0].AmbientColorPower);
}

// [Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"]
float3 MultiBounceAO(float3 baseColor, float ao)
{
	float3 a = 2.0404 * baseColor - 0.3324;
	float3 b = -4.7951 * baseColor + 0.6417;
	float3 c = 2.7552 * baseColor + 0.6903;
	return max(ao, ((ao * a + b) * ao + c) * ao);
}

// [Lagarde et al. 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
float SpecularAOLagarde(float NdotV, float ao, float roughness) 
{
    return saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 GetFresnelFactorSchlick(float3 specularColor, float VdotH)
{
	float Fc = pow(1 - VdotH, 5);
	return Fc + (1 - Fc) * specularColor;
}

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float GetVisibilityFunctionSmithJointApprox(float roughness, float NdotV, float NdotL)
{
	float a = roughness * roughness;
	float visSmithV = NdotL * (NdotV * (1 - a) + a);
	float visSmithL = NdotV * (NdotL * (1 - a) + a);
	return 0.5 * rcp(visSmithV + visSmithL);
}

// [Neubelt et al. 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"]
float GetVisibilityFunctionNeubelt(float NdotV, float NdotL)
{
	return rcp(4 * (NdotL + NdotV - NdotL * NdotV));
}

// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float GetNormalDistributionFunctionGGX(float roughness, float NdotH)
{
	float a2 = pow(roughness, 4);
	float d = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
	return a2 / (PI * d * d);
}

// [Estevez et al. 2017, "Production Friendly Microfacet Sheen BRDF"]
float GetNormalDistributionFunctionCharlie(float roughness, float NdotH) {
    float invAlpha  = 1.0 / (roughness * roughness);
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0 - cos2h, 1e-5);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float3 GetSpecularDirectLightMultiplierMicrofacet(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH)
{
	float D = GetNormalDistributionFunctionGGX(roughness, NdotH);
	float G = GetVisibilityFunctionSmithJointApprox(roughness, NdotV, NdotL);
	float3 F = GetFresnelFactorSchlick(specularColor, VdotH);

	return D * G * F;
}

float3 GetSpecularDirectLightMultiplierMicroflakes(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH)
{
	float D = GetNormalDistributionFunctionCharlie(roughness, NdotH);
	float G = GetVisibilityFunctionNeubelt(NdotV, NdotL);
	float3 F = GetFresnelFactorSchlick(specularColor, VdotH);

	return D * G * F;
}

float GetDiffuseDirectLightMultiplierLambert()
{
	return 1 / PI;
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 GetDiffuseDirectLightMultiplierBurley(float roughness, float NdotV, float NdotL, float VdotH)
{
	float Fd90 = 0.5 + 2 * VdotH * VdotH * roughness;
	float FdV = 1 + (Fd90 - 1) * pow(1 - NdotV, 5);
	float FdL = 1 + (Fd90 - 1) * pow(1 - NdotL, 5);
	return (1 / PI) * FdV * FdL;
}

// [Oren et al. 1994, "Generalization of Lambert’s Reflectance Model"]
float3 GetDiffuseDirectLightMultiplierOrenNayar(float roughness, float3 N, float3 V, float3 L, float NdotV, float NdotL)
{
	float a = roughness * roughness * 0.25;
    float A = 1.0 - 0.5 * (a / (a + 0.33));
    float B = 0.45 * (a / (a + 0.09));
	
	float gamma = dot(V - N * NdotV, L - N * NdotL) / (sqrt(saturate(1.0 - NdotV * NdotV)) * sqrt(saturate(1.0 - NdotL * NdotL)));

    float2 cos_alpha_beta = NdotV < NdotL ? float2(NdotV, NdotL) : float2(NdotL, NdotV);
    float2 sin_alpha_beta = sqrt(saturate(1.0 - cos_alpha_beta * cos_alpha_beta));
    float C = sin_alpha_beta.x * sin_alpha_beta.y / (1e-6 + cos_alpha_beta.y);

    return (1 / PI) * (A + B * max(0.0, gamma) * C);
}

// [Gotanda 2014, "Designing Reflectance Models for New Consoles"]
float3 GetDiffuseDirectLightMultiplierGotanda(float roughness, float NdotV, float NdotL, float VdotL)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float F0 = 0.04;
	float Cosri = VdotL - NdotV * NdotL;
	float Fr = (1 - (0.542026 * a2 + 0.303573 * a) / (a2 + 1.36053)) * (1 - pow(1 - NdotV, 5 - 4 * a2) / (a2 + 1.36053)) * ((-0.733996 * a2 * a + 1.50912 * a2 - 1.16402 * a) * pow(1 - NdotV, 1 + rcp(39 * a2 * a2 + 1)) + 1);
	float Lm = (max(1 - 2 * a, 0) * (1 - pow(1 - NdotL, 5)) + min(2 * a, 1)) * (1 - 0.5 * a * (NdotL - 1)) * NdotL;
	float Vd = (a2 / ((a2 + 0.09) * (1.31072 + 0.995584 * NdotV))) * (1 - pow(1 - NdotL, (1 - 0.3726732 * NdotV * NdotV) / (0.188566 + 0.38841 * NdotV)));
	float Bp = Cosri < 0 ? 1.4 * NdotV * NdotL * Cosri : Cosri;
	float Lr = (21.0 / 20.0) * (1 - F0) * (Fr * Lm + Vd + Bp);
	return (1 / PI) * Lr;
}

// [Chan 2018, "Material Advances in Call of Duty: WWII"]
float3 GetDiffuseDirectLightMultiplierChan(float roughness, float NdotV, float NdotL, float VdotH, float NdotH)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float g = saturate((1.0 / 18.0) * log2(2 * rcp(a2) - 1));

	float F0 = VdotH + pow(1 - VdotH, 5);
	float FdV = 1 - 0.75 * pow(1 - NdotV, 5);
	float FdL = 1 - 0.75 * pow(1 - NdotL, 5);
	
	float Fd = lerp(F0, FdV * FdL, saturate(2.2 * g - 0.5));

	float Fb = ((34.5 * g - 59) * g + 24.5) * VdotH * exp2(-max(73.2 * g - 21.2, 8.9) * sqrt(NdotH));

	return (1 / PI) * (Fd + Fb);
}

// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
float2 GetEnvBRDFApproxLazarov(float roughness, float NdotV)
{
	const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
	const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
	return AB;
}

void GetDirectLightInputPBR(out float3 diffuse, out float3 transmission, out float3 specular, float3 N, float3 V, float3 L, float3 lightColor, float roughness, float3 specularColor, float3 subsurfaceColor, float thickness)
{
	diffuse = 0;
	transmission = 0;
	specular = 0;
	
	float3 H = normalize(V + L);

	float NdotL = dot(N, L);
	float NdotV = dot(N, V);
	float VdotL = dot(V, L);
	float NdotH = dot(N, H);
	float VdotH = dot(V, H);
	
	float satNdotL = clamp(NdotL, 1e-5, 1);
	float satNdotV = saturate(abs(NdotV) + 1e-5);
	float satVdotL = saturate(VdotL);
	float satNdotH = saturate(NdotH);
	float satVdotH = saturate(VdotH);
	
	float diffuseMultiplier = 1;
	[branch] if (pbrData[0].DiffuseModel == TruePBR_Lambert)
	{
		diffuseMultiplier = GetDiffuseDirectLightMultiplierLambert();
	}
	else if (pbrData[0].DiffuseModel == TruePBR_Burley)
	{
		diffuseMultiplier = GetDiffuseDirectLightMultiplierBurley(roughness, satNdotV, satNdotL, satVdotH);
	}
	else if (pbrData[0].DiffuseModel == TruePBR_OrenNayar)
	{
		diffuseMultiplier = GetDiffuseDirectLightMultiplierOrenNayar(roughness, N, V, L, NdotV, NdotL);
	}
	else if (pbrData[0].DiffuseModel == TruePBR_Gotanda)
	{
		diffuseMultiplier = GetDiffuseDirectLightMultiplierGotanda(roughness, satNdotV, satNdotL, satVdotL);
	}
	else
	{
		diffuseMultiplier = GetDiffuseDirectLightMultiplierChan(roughness, satNdotV, satNdotL, satVdotH, satNdotH);
	}

	diffuse += PI * diffuseMultiplier * lightColor * satNdotL;
	specular += PI * GetSpecularDirectLightMultiplierMicrofacet(roughness, specularColor, satNdotL, satNdotV, satNdotH, satVdotH) * lightColor * satNdotL;
	
	float2 specularBRDF = 0;
	[branch] if (pbrData[0].UseMultipleScattering)
	{
		specularBRDF = GetEnvBRDFApproxLazarov(roughness, satNdotV);
		specular *= 1 + specularColor * (1 / (specularBRDF.x + specularBRDF.y) - 1);
	}
	
#	if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
	[branch] if ((PBRFlags & TruePBR_Subsurface) != 0)
	{
		const float subsurfacePower = 12.234;
		float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
		float backScatter = saturate(satNdotL * thickness + (1.0 - thickness)) * 0.5;
		float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - thickness);
		transmission += PI * diffuseMultiplier * subsurfaceColor * subsurface * lightColor;
	}
#	endif
}

float RoughnessToMip(float roughness, float levelCount)
{
    return (levelCount - 1) * roughness * (2.0 - roughness);
}

// [Lagarde et al 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
float3 GetSpecularDominantDirection(float3 N, float3 R, float roughness)
{
	float a = roughness * roughness;
	return lerp(N, R, (1 - a) * (sqrt(1 - a) + a));	
}

void GetAmbientLightInputPBR(out float3 diffuse, out float3 specular, float3 N, float3 V, float3 diffuseColor, float roughness, float3 specularColor, float3 subsurfaceColor, float thickness, float materialAO)
{
	diffuse = 0;
	specular = 0;

	float NdotV = saturate(dot(N, V));
	float3 R = normalize(reflect(-V, N));
	R = GetSpecularDominantDirection(N, R, roughness);

	float3 diffuseIrradiance = 0;
	float3 specularIrradiance = 0;

#	if defined(DYNAMIC_CUBEMAPS)
	uint width = 0, height = 0;
	specularTexture.GetDimensions(width, height);
	uint levelCount = log2(width) + 1;
	float specularLevel = RoughnessToMip(roughness, levelCount);
	
	float ambientScale = 1;
	
	[branch] if (pbrData[0].UseDynamicCubemap)
	{
		[branch] if (pbrData[0].MatchDynamicCubemapToAmbient)
		{
			ambientScale = AdjustAmbientLightColorForPBR(float3(DirectionalAmbient[0].w, DirectionalAmbient[1].w, DirectionalAmbient[2].w)) / (PI * perPassDynamicCubemaps[0].AverageColor);
		}
		
		specularIrradiance += PI * ambientScale * AdjustAmbientLightColorForPBR(specularTexture.SampleLevel(SampColorSampler, R, specularLevel).xyz);
		diffuseIrradiance += PI * ambientScale * AdjustAmbientLightColorForPBR(diffuseTexture.SampleLevel(SampColorSampler, N, 0).xyz);
	}
	else
#	endif
	{
        diffuseIrradiance += AdjustAmbientLightColorForPBR(mul(DirectionalAmbient, float4(N, 1.f)));
        specularIrradiance += AdjustAmbientLightColorForPBR(mul(DirectionalAmbient, float4(R, 1.f)));
    }

    float2 specularBRDF = GetEnvBRDFApproxLazarov(roughness, NdotV);
	
	float3 specularLobeWeight = specularColor * specularBRDF.x + specularBRDF.y;
	float3 diffuseLobeWeight = diffuseColor * (1 - specularLobeWeight);
	[branch] if (pbrData[0].UseMultipleScattering)
	{
		specularLobeWeight *= 1 + specularColor * (1 / (specularBRDF.x + specularBRDF.y) - 1);
	}
	
	diffuse += diffuseIrradiance * diffuseLobeWeight;
	specular += specularIrradiance * specularLobeWeight;
	
	[branch] if ((PBRFlags & TruePBR_Subsurface) != 0)
	{
		float3 subsurfaceSpecularIrradiance = 0;
		
#	if defined(DYNAMIC_CUBEMAPS)
		[branch] if (pbrData[0].UseDynamicCubemap)
		{
			float level = (levelCount - 1) * roughness * roughness + 1 + thickness;
			subsurfaceSpecularIrradiance += PI * ambientScale * AdjustAmbientLightColorForPBR(specularTexture.SampleLevel(SampColorSampler, -V, level).xyz);
		}
		else
#	endif
		{
            subsurfaceSpecularIrradiance += AdjustAmbientLightColorForPBR(mul(DirectionalAmbient, float4(-V, 1.f)));
        }
		
		float attenuation = (1.0 - thickness) / (2.0 * PI);
		diffuse += subsurfaceColor * (diffuseIrradiance + subsurfaceSpecularIrradiance) * attenuation;
	}
	
	float3 diffuseAO = materialAO;
	float3 specularAO = SpecularAOLagarde(NdotV, materialAO, roughness);
	[branch] if (pbrData[0].UseMultiBounceAO)
	{
		diffuseAO = MultiBounceAO(diffuseColor, diffuseAO).y;
		specularAO = MultiBounceAO(specularColor, specularAO).y;
	}
	
	diffuse *= diffuseAO;
	specular *= specularAO;
}