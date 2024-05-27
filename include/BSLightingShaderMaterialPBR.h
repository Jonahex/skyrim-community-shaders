#pragma once

enum class PBRFlags : uint32_t
{
	Subsurface = 1 << 0,
	TwoLayer = 1 << 1,
	ColoredCoat = 1 << 2,
	InterlayerParallax = 1 << 3,
	CoatNormal = 1 << 4,
};

enum class PBRShaderFlags : uint32_t
{
	HasEmissive = 1 << 0,
	HasDisplacement = 1 << 1,
	HasFeaturesTexture0 = 1 << 2,
	HasFeaturesTexture1 = 1 << 3,
	Subsurface = 1 << 4,
	TwoLayer = 1 << 5,
	ColoredCoat = 1 << 6,
	InterlayerParallax = 1 << 7,
	CoatNormal = 1 << 8,
};

class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);

	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);
	inline static constexpr auto EmissiveTexture = static_cast<RE::BSTextureSet::Texture>(2);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto FeaturesTexture0 = static_cast<RE::BSTextureSet::Texture>(7);
	inline static constexpr auto FeaturesTexture1 = static_cast<RE::BSTextureSet::Texture>(6);

	~BSLightingShaderMaterialPBR();

	// override (BSLightingShaderMaterialBase)
	RE::BSShaderMaterial* Create() override;                                                                                      // 01
	void CopyMembers(RE::BSShaderMaterial* that) override;                                                                        // 02
	std::uint32_t ComputeCRC32(uint32_t srcHash) override;                                                                        // 04
	Feature GetFeature() const override;                                                                                          // 06
	void OnLoadTextureSet(std::uint64_t arg1, RE::BSTextureSet* inTextureSet) override;                                           // 08
	void ClearTextures() override;                                                                                                // 09
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;  // 0A
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;                                                                // 0B
	void LoadBinary(RE::NiStream& stream) override;                                                                               // 0D
	
	static BSLightingShaderMaterialPBR* Make();

	float GetRoughnessScale() const;
	float GetSpecularLevel() const;

	float GetDisplacementScale() const;

	const RE::NiColor& GetSubsurfaceColor() const;
	float GetSubsurfaceOpacity() const;

	const RE::NiColor& GetCoatColor() const;
	float GetCoatStrength() const;
	float GetCoatRoughness() const;
	float GetCoatSpecularLevel() const;
	float GetInnerLayerDisplacementOffset() const;

	// members
	RE::BSShaderMaterial::Feature loadedWithFeature = RE::BSShaderMaterial::Feature::kDefault;

	stl::enumeration<PBRFlags> pbrFlags;

	float coatRoughness = 1.f;
	float coatSpecularLevel = 0.04f;
	float innerLayerDisplacementOffset = 0.f;

	// Roughness in r, metallic in g, AO in b, nonmetal reflectance in a
	RE::NiPointer<RE::NiSourceTexture> rmaosTexture;

	// Emission color in rgb
	RE::NiPointer<RE::NiSourceTexture> emissiveTexture;

	// Displacement in r
	RE::NiPointer<RE::NiSourceTexture> displacementTexture;

	// Subsurface map (subsurface color in rgb, thickness in a) / Coat map (coat color in rgb, coat strength in a)
	RE::NiPointer<RE::NiSourceTexture> featuresTexture0;

	// Coat normal map (coat normal in rgb, coat roughness in a)
	RE::NiPointer<RE::NiSourceTexture> featuresTexture1;
};
