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