#pragma once

enum class PBRFlags : uint32_t
{
	TwoSidedFoliage = 1 << 0,
	Subsurface = 1 << 1,
};

enum class PBRShaderFlags : uint32_t
{
	HasEmissive = 1 << 0,
	HasSubsurface = 1 << 1,
	TwoSidedFoliage = 1 << 2,
	Subsurface = 1 << 3,
	HasDisplacement = 1 << 4,
};

class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);

	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);
	inline static constexpr auto EmissiveTexture = static_cast<RE::BSTextureSet::Texture>(2);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto SubsurfaceTexture = static_cast<RE::BSTextureSet::Texture>(7);

	inline static constexpr uint32_t Version = 2;

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
	void SaveBinary(RE::NiStream& stream) override;                                                                               // 0C
	void LoadBinary(RE::NiStream& stream) override;                                                                               // 0D

	static BSLightingShaderMaterialPBR* Make();

	// members
	stl::enumeration<PBRFlags> pbrFlags;

	RE::NiPointer<RE::NiSourceTexture> rmaosTexture;
	RE::NiPointer<RE::NiSourceTexture> emissiveTexture;
	RE::NiPointer<RE::NiSourceTexture> displacementTexture;
	RE::NiPointer<RE::NiSourceTexture> subsurfaceTexture;

	float roughnessScale = 1.f;
	float metallicScale = 1.f;
	float specularLevel = 0.04f;
	float displacementScale = 1.f;

	RE::NiColorA subsurfaceColor;
};
