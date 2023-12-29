#pragma once

class BSLightingShaderMaterialPBRLandscape : public RE::BSLightingShaderMaterialBase
{
public:
	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(33);

	inline static constexpr auto BcdTexture = static_cast<RE::BSTextureSet::Texture>(0);
	inline static constexpr auto NormalTexture = static_cast<RE::BSTextureSet::Texture>(1);
	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);

	inline static constexpr uint32_t NumTiles = 6;

	BSLightingShaderMaterialPBRLandscape();
	~BSLightingShaderMaterialPBRLandscape();

	// override (BSLightingShaderMaterialBase)
	RE::BSShaderMaterial* Create() override;                                                                                      // 01
	void CopyMembers(RE::BSShaderMaterial* that) override;                                                                        // 02
	Feature GetFeature() const override;                                                                                          // 06
	void ClearTextures() override;                                                                                                // 09
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;  // 0A
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;                                                                // 0B

	static BSLightingShaderMaterialPBRLandscape* Make();

	// members
	std::uint32_t numLandscapeTextures = 0;
	RE::NiPointer<RE::NiSourceTexture> landscapeBCDTextures[NumTiles - 1];
	RE::NiPointer<RE::NiSourceTexture> landscapeNormalTextures[NumTiles - 1];
	RE::NiPointer<RE::NiSourceTexture> terrainOverlayTexture;
	RE::NiPointer<RE::NiSourceTexture> terrainNoiseTexture;
	RE::NiColorA landBlendParams;
	RE::NiPointer<RE::NiSourceTexture> landscapeRMAOSTextures[NumTiles];
	std::array<bool, NumTiles> isPbr;
	float terrainTexOffsetX = 0.f;
	float terrainTexOffsetY = 0.f;
	float terrainTexFade = 0.f;
};
