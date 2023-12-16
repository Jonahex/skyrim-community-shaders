#include "BSLightingShaderMaterialPBR.h"

BSLightingShaderMaterialPBR::~BSLightingShaderMaterialPBR()
{}

BSLightingShaderMaterialPBR* BSLightingShaderMaterialPBR::Make()
{
	return new BSLightingShaderMaterialPBR;
}

RE::BSShaderMaterial* BSLightingShaderMaterialPBR::Create()
{
	return Make();
}

void BSLightingShaderMaterialPBR::CopyMembers(RE::BSShaderMaterial* that)
{
	BSLightingShaderMaterialBase::CopyMembers(that);

	auto* pbrThat = static_cast<BSLightingShaderMaterialPBR*>(that);
	if (rmaosTexture != pbrThat->rmaosTexture) {
		rmaosTexture = pbrThat->rmaosTexture;
	}
	if (emissiveTexture != pbrThat->emissiveTexture) {
		emissiveTexture = pbrThat->emissiveTexture;
	}

	roughnessScale = pbrThat->roughnessScale;
	metallicScale = pbrThat->metallicScale;
	specularLevel = pbrThat->specularLevel;
}

std::uint32_t BSLightingShaderMaterialPBR::ComputeCRC32(uint32_t srcHash)
{
	struct HashContainer
	{
		uint32_t rmaodHash = 0;
		uint32_t emissiveHash = 0;
		uint32_t baseHash = 0;
		float roughnessScale = 0.f;
		float metallicScale = 0.f;
		float specularLevel = 0.f;
	} hashes;

	if (textureSet != nullptr)
	{
		hashes.rmaodHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(RmaosTexture));
		hashes.emissiveHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(EmissiveTexture));
	}
	hashes.roughnessScale = roughnessScale;
	hashes.metallicScale = metallicScale;
	hashes.specularLevel = specularLevel;

	hashes.baseHash = BSLightingShaderMaterialBase::ComputeCRC32(srcHash);

	return RE::detail::GenerateCRC32({ reinterpret_cast<const std::uint8_t*>(&hashes), sizeof(HashContainer) });
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBR::GetFeature() const
{
	return FEATURE;
}

void BSLightingShaderMaterialPBR::OnLoadTextureSet(std::uint64_t arg1, RE::BSTextureSet* inTextureSet)
{
	const auto& stateData = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();

	if (diffuseTexture == nullptr || diffuseTexture == stateData.defaultTextureNormalMap) {
		BSLightingShaderMaterialBase::OnLoadTextureSet(arg1, inTextureSet);

		auto* lock = &unk98;
		while (_InterlockedCompareExchange(lock, 1, 0)) {
			Sleep(0);
		}
		_mm_mfence();

		logger::info("OnLoadTextureSet:LoadPbr");
		if (inTextureSet != nullptr) {
			textureSet = RE::NiPointer(inTextureSet);
		}
		if (textureSet != nullptr) {
			textureSet->SetTexture(RmaosTexture, rmaosTexture);
			textureSet->SetTexture(EmissiveTexture, emissiveTexture);
		}

		if (lock != nullptr) {
			*lock = 0;
			_mm_mfence();
		}
	}
}

void BSLightingShaderMaterialPBR::ClearTextures()
{
	BSLightingShaderMaterialBase::ClearTextures();
	rmaosTexture.reset();
	emissiveTexture.reset();
}

void BSLightingShaderMaterialPBR::ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN)
{
	BSLightingShaderMaterialBase::ReceiveValuesFromRootMaterial(skinned, rimLighting, softLighting, backLighting, MSN);
	const auto& stateData = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();
	if (rmaosTexture == nullptr) {
		rmaosTexture = stateData.defaultTextureWhite;
	}
	if (emissiveTexture == nullptr) {
		emissiveTexture = stateData.defaultTextureBlack;
	}
}

uint32_t BSLightingShaderMaterialPBR::GetTextures(RE::NiSourceTexture** textures)
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
	if (rmaosTexture != nullptr) {
		textures[textureIndex++] = rmaosTexture.get();
	}
	if (emissiveTexture != nullptr) {
		textures[textureIndex++] = emissiveTexture.get();
	}

	return textureIndex;
}

void BSLightingShaderMaterialPBR::SaveBinary(RE::NiStream& stream)
{
	stream.iStr->write(&Version, 1);

	BSLightingShaderMaterialBase::SaveBinary(stream);

	stream.iStr->write(&roughnessScale, 1);
	stream.iStr->write(&metallicScale, 1);
	stream.iStr->write(&specularLevel, 1);
}

void BSLightingShaderMaterialPBR::LoadBinary(RE::NiStream& stream)
{
	uint32_t pbrVersion = BSLightingShaderMaterialPBR::Version;
	stream.iStr->read(&pbrVersion, 1);

	BSLightingShaderMaterialBase::LoadBinary(stream);

	stream.iStr->read(&roughnessScale, 1);
	stream.iStr->read(&metallicScale, 1);
	stream.iStr->read(&specularLevel, 1);
}

