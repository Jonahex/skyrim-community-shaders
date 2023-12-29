#include "BSLightingShaderMaterialPBRLandscape.h"

BSLightingShaderMaterialPBRLandscape::BSLightingShaderMaterialPBRLandscape()
{
	std::fill(isPbr.begin(), isPbr.end(), false);
}

BSLightingShaderMaterialPBRLandscape::~BSLightingShaderMaterialPBRLandscape()
{}

BSLightingShaderMaterialPBRLandscape* BSLightingShaderMaterialPBRLandscape::Make()
{
	return new BSLightingShaderMaterialPBRLandscape;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBRLandscape::Create()
{
	return Make();
}

void BSLightingShaderMaterialPBRLandscape::CopyMembers(RE::BSShaderMaterial* that)
{
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBRLandscape*>(that);

	pbrThat->numLandscapeTextures = numLandscapeTextures;

	for (uint32_t textureIndex = 0; textureIndex < NumTiles; ++textureIndex)
	{
		pbrThat->landscapeBCDTextures[textureIndex] = landscapeBCDTextures[textureIndex];
		pbrThat->landscapeNormalTextures[textureIndex] = landscapeNormalTextures[textureIndex];
		pbrThat->landscapeRMAOSTextures[textureIndex] = landscapeRMAOSTextures[textureIndex];
	}
	pbrThat->terrainOverlayTexture = terrainOverlayTexture;
	pbrThat->terrainNoiseTexture = terrainNoiseTexture;
	pbrThat->landBlendParams = landBlendParams;
	pbrThat->terrainTexOffsetX = terrainTexOffsetX;
	pbrThat->terrainTexOffsetY = terrainTexOffsetY;
	pbrThat->terrainTexFade = terrainTexFade;
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBRLandscape::GetFeature() const
{
	return RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
	//return FEATURE;
}

void BSLightingShaderMaterialPBRLandscape::ClearTextures()
{
	BSLightingShaderMaterialBase::ClearTextures();
	for (auto& texture : landscapeBCDTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeNormalTextures) {
		texture.reset();
	}
	for (auto& texture : landscapeRMAOSTextures) {
		texture.reset();
	}
	terrainOverlayTexture.reset();
	terrainNoiseTexture.reset();
}

void BSLightingShaderMaterialPBRLandscape::ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN)
{
	BSLightingShaderMaterialBase::ReceiveValuesFromRootMaterial(skinned, rimLighting, softLighting, backLighting, MSN);
	const auto& stateData = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();
	if (terrainOverlayTexture == nullptr) {
		terrainOverlayTexture = stateData.defaultTextureNormalMap;
	}
	if (terrainNoiseTexture == nullptr) {
		terrainNoiseTexture = stateData.defaultTextureNormalMap;
	}
	for (uint32_t textureIndex = 0; textureIndex < numLandscapeTextures; ++textureIndex)
	{
		if (landscapeBCDTextures[textureIndex] == nullptr) {
			landscapeBCDTextures[textureIndex] = stateData.defaultTextureBlack;
		}
		if (landscapeNormalTextures[textureIndex] == nullptr) {
			landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
		}
		if (landscapeRMAOSTextures[textureIndex] == nullptr) {
			landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
		}
	}
}

uint32_t BSLightingShaderMaterialPBRLandscape::GetTextures(RE::NiSourceTexture** textures)
{
	uint32_t textureIndex = 0;
	if (diffuseTexture != nullptr) {
		textures[textureIndex++] = diffuseTexture.get();
	}
	if (normalTexture != nullptr) {
		textures[textureIndex++] = normalTexture.get();
	}
	if (rimSoftLightingTexture != nullptr) {
		textures[textureIndex++] = rimSoftLightingTexture.get();
	}
	if (specularBackLightingTexture != nullptr) {
		textures[textureIndex++] = specularBackLightingTexture.get();
	}
	for (uint32_t tileIndex = 0; tileIndex < numLandscapeTextures; ++tileIndex) {
		if (landscapeBCDTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeBCDTextures[tileIndex].get();
		}
		if (landscapeNormalTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeNormalTextures[tileIndex].get();
		}
		if (landscapeRMAOSTextures[tileIndex] != nullptr) {
			textures[textureIndex++] = landscapeRMAOSTextures[tileIndex].get();
		}
	}
	if (terrainOverlayTexture != nullptr) {
		textures[textureIndex++] = terrainOverlayTexture.get();
	}
	if (terrainNoiseTexture != nullptr) {
		textures[textureIndex++] = terrainNoiseTexture.get();
	}

	return textureIndex;
}