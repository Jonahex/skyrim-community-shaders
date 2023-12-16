#pragma once

class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);

	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);
	inline static constexpr auto EmissiveTexture = static_cast<RE::BSTextureSet::Texture>(2);

	inline static constexpr uint32_t Version = 0;

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
	RE::NiPointer<RE::NiSourceTexture> rmaosTexture;
	RE::NiPointer<RE::NiSourceTexture> emissiveTexture;

	float roughnessScale = 1.f;
	float metallicScale = 1.f;
	float specularLevel = 0.04;
};
