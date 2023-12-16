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

void GetDirectLightInputPBR(out float3 diffuse, out float3 specular, float3 N, float3 V, float3 L, float3 lightColor, float roughness, float3 specularColor)
{
	float3 H = normalize(V + L);

	float NdotV = clamp(dot(N, V), 1e-5, 1);
	float NdotL = clamp(dot(N, L), 1e-5, 1);
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));
	float VdotL = saturate(dot(V, L));

	//diffuse = PI * GetDiffuseDirectLightMultiplierLambert() * lightColor * NdotL;
	//diffuse = PI * GetDiffuseDirectLightMultiplierBurley(roughness, NdotV, NdotL, VdotH) * lightColor * NdotL;
	//diffuse = PI * GetDiffuseDirectLightMultiplierOrenNayar(roughness, NdotV, NdotL, VdotL) * lightColor * NdotL;
	//diffuse = PI * GetDiffuseDirectLightMultiplierGotanda(roughness, NdotV, NdotL, VdotL) * lightColor * NdotL;
	diffuse = PI * GetDiffuseDirectLightMultiplierChan(roughness, NdotV, NdotL, VdotH, NdotH) * lightColor * NdotL;
	specular = PI * GetSpecularDirectLightMultiplierMicrofacet(roughness, specularColor, NdotL, NdotV, NdotH, VdotH) * lightColor * NdotL;
	
	float3 W, E;
	ComputeGGXSpecularEnergyTerms(W, E, roughness, NdotV, specularColor);
	
	diffuse *= 1 - RGBToLuminanceAlternative(E);
	specular *= W;
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

float3 GetPBRAmbientSpecular(float3 N, float3 V, float roughness, float3 specularColor)
{
	float NdotV = saturate(dot(N, V));
	float3 R = normalize(reflect(-V, N));
	
    //R = lerp(N, R, saturate(1.35 * (1.0 - roughness)));
    //R = R + saturate(-dot(polygonN, R)) * polygonN; // clip light from neagative subspace of polygon

	float3 specularIrradiance = 0;

#	if defined(DYNAMIC_CUBEMAPS)
	uint levelCount = 0, width = 0, height = 0;
	specularTexture.GetDimensions(0, width, height, levelCount);
	float level = ComputeReflectionCaptureMipFromRoughness(roughness, levelCount);
	specularIrradiance = specularTexture.SampleLevel(SampColorSampler, R, level).rgb;
#	endif

	specularIrradiance = sRGB2Lin(specularIrradiance);

	// Split-sum approximation factors for Cook-Torrance specular BRDF.
#	if defined(DYNAMIC_CUBEMAPS)
	float2 specularBRDF = specularBRDF_LUT.Sample(LinearSampler, float2(NdotV, roughness));
#	else
	float2 specularBRDF = EnvBRDFApprox(specularColor, roughness, NdotV);
#	endif

	// Roughness dependent fresnel
	// https://www.jcgt.org/published/0008/01/03/paper.pdf
	float3 Fr = max(1 - roughness, specularColor) - specularColor;
	float3 S = specularColor + Fr * pow(1.0 - NdotV, 5.0);

	return PI * specularIrradiance * (S * specularBRDF.x + specularBRDF.y);
}