#include "BSLightingShaderMaterialPBR.h"

#include "State.h"

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

	pbrFlags = pbrThat->pbrFlags;

	if (rmaosTexture != pbrThat->rmaosTexture) {
		rmaosTexture = pbrThat->rmaosTexture;
	}
	if (emissiveTexture != pbrThat->emissiveTexture) {
		emissiveTexture = pbrThat->emissiveTexture;
	}
	if (displacementTexture != pbrThat->displacementTexture) {
		displacementTexture = pbrThat->displacementTexture;
	}
	if (subsurfaceTexture != pbrThat->subsurfaceTexture) {
		subsurfaceTexture = pbrThat->subsurfaceTexture;
	}
}

std::uint32_t BSLightingShaderMaterialPBR::ComputeCRC32(uint32_t srcHash)
{
	struct HashContainer
	{
		uint32_t pbrFlags = 0;
		uint32_t rmaodHash = 0;
		uint32_t emissiveHash = 0;
		uint32_t displacementHash = 0;
		uint32_t subsurfaceHash = 0;
		uint32_t baseHash = 0;
	} hashes;

	hashes.pbrFlags = pbrFlags.underlying();
	if (textureSet != nullptr)
	{
		hashes.rmaodHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(RmaosTexture));
		hashes.emissiveHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(EmissiveTexture));
		hashes.displacementHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(DisplacementTexture));
		hashes.subsurfaceHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(SubsurfaceTexture));
	}

	hashes.baseHash = BSLightingShaderMaterialBase::ComputeCRC32(srcHash);

	return RE::detail::GenerateCRC32({ reinterpret_cast<const std::uint8_t*>(&hashes), sizeof(HashContainer) });
}

RE::BSShaderMaterial::Feature BSLightingShaderMaterialPBR::GetFeature() const
{
	return RE::BSShaderMaterial::Feature::kDefault;
	//return FEATURE;
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

		if (inTextureSet != nullptr) {
			textureSet = RE::NiPointer(inTextureSet);
		}
		if (textureSet != nullptr) {
			textureSet->SetTexture(RmaosTexture, rmaosTexture);
			textureSet->SetTexture(EmissiveTexture, emissiveTexture);
			textureSet->SetTexture(DisplacementTexture, displacementTexture);
			textureSet->SetTexture(SubsurfaceTexture, subsurfaceTexture);

			if (auto* bgsTextureSet = netimmerse_cast<RE::BGSTextureSet*>(inTextureSet); bgsTextureSet != nullptr) {
				if (auto* textureSetData = State::GetSingleton()->GetPBRTextureSetData(bgsTextureSet)) {
					specularColorScale = textureSetData->roughnessScale;
					specularPower = textureSetData->specularLevel;
					rimLightPower = textureSetData->displacementScale;
					specularColor = textureSetData->subsurfaceColor;
					subSurfaceLightRolloff = textureSetData->subsurfaceOpacity;
				}
			}
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
	displacementTexture.reset();
	subsurfaceTexture.reset();
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
	if (displacementTexture == nullptr) {
		displacementTexture = stateData.defaultTextureBlack;
	}
	if (subsurfaceTexture == nullptr) {
		subsurfaceTexture = stateData.defaultTextureWhite;
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
	if (displacementTexture != nullptr) {
		textures[textureIndex++] = displacementTexture.get();
	}
	if (subsurfaceTexture != nullptr) {
		textures[textureIndex++] = subsurfaceTexture.get();
	}

	return textureIndex;
}

float BSLightingShaderMaterialPBR::GetRoughnessScale() const
{
	return specularColorScale;
}

float BSLightingShaderMaterialPBR::GetSpecularLevel() const
{
	return specularPower;
}

float BSLightingShaderMaterialPBR::GetDisplacementScale() const
{
	return rimLightPower;
}

const RE::NiColor& BSLightingShaderMaterialPBR::GetSubsurfaceColor() const
{
	return specularColor;
}

float BSLightingShaderMaterialPBR::GetSubsurfaceOpacity() const
{
	return subSurfaceLightRolloff;
}
