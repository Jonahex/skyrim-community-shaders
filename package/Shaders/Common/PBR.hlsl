float3 AOMultiBounce(float3 baseColor, float ao)
{
	float3 a = 2.0404 * baseColor - 0.3324;
	float3 b = -4.7951 * baseColor + 0.6417;
	float3 c = 2.7552 * baseColor + 0.6903;
	return max(ao, ((ao * a + b) * ao + c) * ao);
}

float GetDiffuseDirectLightMultiplierLambert()
{
	return 1 / PI;
}

float3 GetDiffuseDirectLightMultiplierBurley(float roughness, float NdotV, float NdotL, float VdotH)
{
	float FD90 = 0.5 + 2 * VdotH * VdotH * roughness;
	float FdV = 1 + (FD90 - 1) * pow(1 - NdotV, 5);
	float FdL = 1 + (FD90 - 1) * pow(1 - NdotL, 5);
	return (1 / PI) * FdV * FdL;
}

float3 GetDiffuseDirectLightMultiplierOrenNayar(float roughness, float NdotV, float NdotL, float VdotL)
{
	float a = roughness * roughness;
	float s = a;  // / ( 1.29 + 0.5 * a );
	float s2 = s * s;
	float Cosri = VdotL - NdotV * NdotL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * (Cosri >= 0 ? rcp(max(NdotL, NdotV)) : 1);
	return (1 / PI) * (C1 + C2) * (1 + roughness * 0.5);
}

float3 GetDiffuseDirectLightMultiplierGotanda(float roughness, float NdotV, float NdotL, float VdotL)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float F0 = 0.04;
	float Cosri = VdotL - NdotV * NdotL;
	float a2_13 = a2 + 1.36053;
	float Fr = (1 - (0.542026 * a2 + 0.303573 * a) / a2_13) * (1 - pow(1 - NdotV, 5 - 4 * a2) / a2_13) * ((-0.733996 * a2 * a + 1.50912 * a2 - 1.16402 * a) * pow(1 - NdotV, 1 + rcp(39 * a2 * a2 + 1)) + 1);
	//float Fr = ( 1 - 0.36 * a ) * ( 1 - pow( 1 - NdotV, 5 - 4*a2 ) / a2_13 ) * ( -2.5 * roughness * ( 1 - NdotV ) + 1 );
	float Lm = (max(1 - 2 * a, 0) * (1 - pow(1 - NdotL, 5)) + min(2 * a, 1)) * (1 - 0.5 * a * (NdotL - 1)) * NdotL;
	float Vd = (a2 / ((a2 + 0.09) * (1.31072 + 0.995584 * NdotV))) * (1 - pow(1 - NdotL, (1 - 0.3726732 * NdotV * NdotV) / (0.188566 + 0.38841 * NdotV)));
	float Bp = Cosri < 0 ? 1.4 * NdotV * NdotL * Cosri : Cosri;
	float Lr = (21.0 / 20.0) * (1 - F0) * (Fr * Lm + Vd + Bp);
	return (1 / PI) * Lr;
}

float3 GetDiffuseDirectLightMultiplierChan(float roughness, float NdotV, float NdotL, float VdotH, float NdotH)
{
	float a = roughness * roughness;
	float a2 = a * a;
	// a2 = 2 / ( 1 + exp2( 18 * g )
	float g = saturate((1.0 / 18.0) * log2(2 * rcp(a2) - 1));

	float F0 = VdotH + pow(1 - VdotH, 5);
	float FdV = 1 - 0.75 * pow(1 - NdotV, 5);
	float FdL = 1 - 0.75 * pow(1 - NdotL, 5);

	// Rough (F0) to smooth (FdV * FdL) response interpolation
	float Fd = lerp(F0, FdV * FdL, saturate(2.2 * g - 0.5));

	// Retro reflectivity contribution.
	float Fb = ((34.5 * g - 59) * g + 24.5) * VdotH * exp2(-max(73.2 * g - 21.2, 8.9) * sqrt(NdotH));

	return (1 / PI) * (Fd + Fb);
}

float3 GetFresnelFactorSchlick(float3 specularColor, float VdotH)
{
	float Fc = pow(1 - VdotH, 5);  // 1 sub, 3 mul
	//return Fc + (1 - Fc) * specularColor;		// 1 add, 3 mad

	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	return saturate(50.0 * specularColor.g) * Fc + (1 - Fc) * specularColor;
}

float3 GetFresnelFactorSchlick2(float3 specularColor, float NdotL)
{
    return specularColor + (1 - specularColor) * (pow(1 - NdotL, 5));
}

float GetGeometryFunctionSmithJointApprox(float roughness, float NdotV, float NdotL)
{
	float a = roughness * roughness;
	float Vis_SmithV = NdotL * (NdotV * (1 - a) + a);
	float Vis_SmithL = NdotV * (NdotL * (1 - a) + a);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

float GetDistributionFunctionGGX(float roughness, float NdotH)
{
	float a2 = pow(roughness, 4);
	float d = (NdotH * a2 - NdotH) * NdotH + 1;  // 2 mad
	return a2 / (PI * d * d);                    // 4 mul, 1 rcp
}

float3 GetSpecularDirectLightMultiplierMicrofacet(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH)
{
	float D = GetDistributionFunctionGGX(roughness, NdotH);
	float G = GetGeometryFunctionSmithJointApprox(roughness, NdotV, NdotL);
	float3 F = GetFresnelFactorSchlick(specularColor, VdotH);
	//float3 F = GetFresnelFactorSchlick2(specularColor, NdotL);

	return D * G * F;
}

float2 GGXEnergyLookup(float roughness, float NdotV)
{
	const float r = roughness;
	const float c = NdotV;
	const float E = 1.0 - saturate(pow(r, c / r) * ((r * c + 0.0266916) / (0.466495 + c)));
	const float Ef = pow(1 - c, 5) * pow(2.36651 * pow(c, 4.7703 * r) + 0.0387332, r);
	return float2(E, Ef);
}

void ComputeFresnelEnergyTerms(out float3 W, out float3 E, float2 energy, float3 F0, float3 F90)
{
	W = 1.0 + F0 * ((1 - energy.x) / energy.x);
	E = W * (energy.x * F0 + energy.y * (F90 - F0));
}

void ComputeGGXSpecularEnergyTerms(out float3 W, out float3 E, float roughness, float NdotV, float3 F0)
{
	const float F90 = saturate(50.0 * F0.g);
	ComputeFresnelEnergyTerms(W, E, GGXEnergyLookup(roughness, NdotV), F0, F90);
}

float3 TransmittanceToExtinction(float3 transmittanceColor, float thicknessMeters)
{
	const float MinTransmittance = 0.000000000001f;
	const float MinMFPMeter = 0.000000000001f;
	return -log(clamp(transmittanceColor, MinTransmittance, 1.0f)) / max(MinMFPMeter, thicknessMeters);
}

float3 ExtinctionToTransmittance(float3 extinction, float thicknessMeters)
{
	return exp(-extinction * thicknessMeters);
}

void GetDirectLightInputPBR(out float3 diffuse, out float3 transmission, out float3 specular, float3 N, float3 V, float3 L, float3 lightColor, float roughness, float3 specularColor, float3 subsurfaceColor, float subsurfaceOpacity, float shadow, float ao)
{
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

	//diffuse = PI * GetDiffuseDirectLightMultiplierLambert() * lightColor * satNdotL;
	//diffuse = PI * GetDiffuseDirectLightMultiplierBurley(roughness, satNdotV, satNdotL, satVdotH) * lightColor * satNdotL;
	//diffuse = PI * GetDiffuseDirectLightMultiplierOrenNayar(roughness, satNdotV, satNdotL, satVdotL) * lightColor * satNdotL;
	//diffuse = PI * GetDiffuseDirectLightMultiplierGotanda(roughness, satNdotV, satNdotL, satVdotL) * lightColor * satNdotL;
	diffuse = PI * GetDiffuseDirectLightMultiplierChan(roughness, saturate(NdotV), satNdotL, satVdotH, satNdotH) * lightColor * satNdotL;
	specular = PI * GetSpecularDirectLightMultiplierMicrofacet(roughness, specularColor, satNdotL, satNdotV, satNdotH, satVdotH) * lightColor * satNdotL;
	
	float3 W, E;
	ComputeGGXSpecularEnergyTerms(W, E, roughness, satNdotV, specularColor);
	diffuse *= 1 - RGBToLuminanceAlternative(E);
	specular *= W;
	
	[branch] if ((uint(PBRParams.w) & 4) != 0) // Two-sided foliage
	{
		float wrap = 0.5;
		float wrapNdotL = saturate((-NdotL + wrap) / ((1 + wrap) * (1 + wrap)));
		float scatter = GetDistributionFunctionGGX(0.6, saturate(-VdotL));

		transmission = PI * wrapNdotL * scatter * lightColor * subsurfaceColor;
	}
	else if ((uint(PBRParams.w) & 8) != 0) // Subsurface
	{
		float scatter = pow(saturate(-VdotL), 12) * lerp(3, .1f, subsurfaceOpacity);
		float wrappedDiffuse = pow(saturate(NdotL * (1.f / 1.5f) + (0.5f / 1.5f)), 1.5f) * (2.5f / 1.5f);
		float normalContribution = lerp(1.f, wrappedDiffuse, subsurfaceOpacity);
		float backScatter = ao * normalContribution / (PI * 2);
		float3 extinctionCoefficients = TransmittanceToExtinction(subsurfaceColor, 0.15f);
		float3 rawTransmittedColor = ExtinctionToTransmittance(extinctionCoefficients, 1.0f);
		float3 transmittedColor = HSV2Lin(float3(Lin2HSV(rawTransmittedColor).xy, Lin2HSV(subsurfaceColor).z));

		transmission = PI * lerp(backScatter, 1, scatter) * lerp(transmittedColor, subsurfaceColor, shadow) * lightColor;
	}
}

float ComputeReflectionCaptureMipFromRoughness(float roughness, float cubemapMaxMip)
{
	const float RoughestMip = 1;
	const float RoughnessMipScale = 1.2;
	// Heuristic that maps roughness to mip level
	// This is done in a way such that a certain mip level will always have the same roughness, regardless of how many mips are in the texture
	// Using more mips in the cubemap just allows sharper reflections to be supported
	float LevelFrom1x1 = RoughestMip - RoughnessMipScale * log2(max(roughness, 0.001));
	return cubemapMaxMip - 1 - LevelFrom1x1;
}

void GetAmbientLightInputPBR(out float3 diffuse, out float3 specular, float3 N, float3 V, float3 diffuseColor, float roughness, float3 specularColor, float3 subsurfaceColor, float ao)
{
	float NdotV = saturate(dot(N, V));
	float3 R0 = normalize(reflect(-V, N));
	
	// Point lobe in off-specular peak direction
	float a = roughness * roughness;
	float3 R = lerp(N, R0, (1 - a) * (sqrt(1 - a) + a));
	
    //R = lerp(N, R, saturate(1.35 * (1.0 - roughness)));
    //R = R + saturate(-dot(polygonN, R)) * polygonN; // clip light from neagative subspace of polygon

	float3 diffuseIrradiance = 0;
	float3 specularIrradiance = 0;

#	if defined(DYNAMIC_CUBEMAPS)
	uint levelCount = 0, width = 0, height = 0;
	specularTexture.GetDimensions(0, width, height, levelCount);
	float diffuseLevel = levelCount - 4;
	float specularLevel = ComputeReflectionCaptureMipFromRoughness(roughness, levelCount);
	diffuseIrradiance = specularTexture.SampleLevel(SampColorSampler, N, diffuseLevel).rgb;
	specularIrradiance = specularTexture.SampleLevel(SampColorSampler, R, specularLevel).rgb;
#	endif

	diffuseIrradiance = sRGB2Lin(diffuseIrradiance);
	specularIrradiance = sRGB2Lin(specularIrradiance);

	// Split-sum approximation factors for Cook-Torrance specular BRDF.
#	if defined(DYNAMIC_CUBEMAPS)
	float2 specularBRDF = specularBRDF_LUT.Sample(LinearSampler, float2(NdotV, roughness)).xy;
#	else
	float2 specularBRDF = EnvBRDFApprox(specularColor, roughness, NdotV);
#	endif

	diffuse = PI * diffuseIrradiance * diffuseColor;
	specular = PI * specularIrradiance * (specularColor * specularBRDF.x + saturate(50.0 * specularColor.g) * specularBRDF.y);

	// Subsurface
	[branch] if((uint(PBRParams.w) & 8) != 0) // Subsurface
	{
		float dependentSplit = 0.5f;
		diffuse += PI * diffuseIrradiance * subsurfaceColor * dependentSplit;
		
		float3 subsurfaceSpecularIrradiance = 0.f;
#	if defined(DYNAMIC_CUBEMAPS)
		float subsurfaceSpecularLevel = diffuseLevel - 2.5f;
		subsurfaceSpecularIrradiance = specularTexture.SampleLevel(SampColorSampler, -V, diffuseLevel - 2.5f).rgb;
#	endif
		specular += PI * subsurfaceSpecularIrradiance * subsurfaceColor * (ao * (1.0f - dependentSplit));
	}
	
	// Roughness dependent fresnel
	// https://www.jcgt.org/published/0008/01/03/paper.pdf
	//float3 Fr = max(1 - roughness, specularColor) - specularColor;
	//float3 S = specularColor + Fr * pow(1.0 - NdotV, 5.0);
	//
	//specular = PI * specularIrradiance * (S * specularBRDF.x + specularBRDF.y);
}