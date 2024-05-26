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

	loadedWithFeature = pbrThat->loadedWithFeature;
	pbrFlags = pbrThat->pbrFlags;
	coatRoughness = pbrThat->coatRoughness;
	coatSpecularLevel = pbrThat->coatSpecularLevel;

	if (rmaosTexture != pbrThat->rmaosTexture) {
		rmaosTexture = pbrThat->rmaosTexture;
	}
	if (emissiveTexture != pbrThat->emissiveTexture) {
		emissiveTexture = pbrThat->emissiveTexture;
	}
	if (displacementTexture != pbrThat->displacementTexture) {
		displacementTexture = pbrThat->displacementTexture;
	}
	if (featuresTexture0 != pbrThat->featuresTexture0) {
		featuresTexture0 = pbrThat->featuresTexture0;
	}
	if (featuresTexture1 != pbrThat->featuresTexture1) {
		featuresTexture1 = pbrThat->featuresTexture1;
	}
}

std::uint32_t BSLightingShaderMaterialPBR::ComputeCRC32(uint32_t srcHash)
{
	struct HashContainer
	{
		uint32_t pbrFlags = 0;
		float coatRoughness = 0.f;
		float coatSpecularLevel = 0.f;
		uint32_t rmaodHash = 0;
		uint32_t emissiveHash = 0;
		uint32_t displacementHash = 0;
		uint32_t features0Hash = 0;
		uint32_t features1Hash = 0;
		uint32_t baseHash = 0;
	} hashes;

	hashes.pbrFlags = pbrFlags.underlying();
	hashes.coatRoughness = coatRoughness * 100.f;
	hashes.coatSpecularLevel = coatSpecularLevel * 100.f;
	if (textureSet != nullptr)
	{
		hashes.rmaodHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(RmaosTexture));
		hashes.emissiveHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(EmissiveTexture));
		hashes.displacementHash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(DisplacementTexture));
		hashes.features0Hash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(FeaturesTexture0));
		hashes.features1Hash = RE::BSCRC32<const char*>()(textureSet->GetTexturePath(FeaturesTexture1));
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
			textureSet->SetTexture(FeaturesTexture0, featuresTexture0);
			textureSet->SetTexture(FeaturesTexture1, featuresTexture1);

			if (auto* bgsTextureSet = netimmerse_cast<RE::BGSTextureSet*>(inTextureSet); bgsTextureSet != nullptr) {
				if (auto* textureSetData = State::GetSingleton()->GetPBRTextureSetData(bgsTextureSet)) {
					specularColorScale = textureSetData->roughnessScale;
					specularPower = textureSetData->specularLevel;
					rimLightPower = textureSetData->displacementScale;

					if (pbrFlags.any(PBRFlags::Subsurface)) {
						specularColor = textureSetData->subsurfaceColor;
						subSurfaceLightRolloff = textureSetData->subsurfaceOpacity;
					} else if (pbrFlags.any(PBRFlags::TwoLayer)) {
						specularColor = textureSetData->coatColor;
						subSurfaceLightRolloff = textureSetData->coatStrength;
						coatRoughness = textureSetData->coatRoughness;
						coatSpecularLevel = textureSetData->coatSpecularLevel;
					}
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
	featuresTexture0.reset();
	featuresTexture1.reset();
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
	if (featuresTexture0 == nullptr) {
		featuresTexture0 = stateData.defaultTextureWhite;
	}
	if (featuresTexture1 == nullptr) {
		featuresTexture1 = stateData.defaultTextureWhite;
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
	if (featuresTexture0 != nullptr) {
		textures[textureIndex++] = featuresTexture0.get();
	}
	if (featuresTexture1 != nullptr) {
		textures[textureIndex++] = featuresTexture1.get();
	}

	return textureIndex;
}

void BSLightingShaderMaterialPBR::LoadBinary(RE::NiStream& stream)
{
	BSLightingShaderMaterialBase::LoadBinary(stream);

	if (loadedWithFeature == RE::BSLightingShaderMaterial::Feature::kMultilayerParallax)
	{
		stream.iStr->read(&coatRoughness, 1);
		stream.iStr->read(&coatSpecularLevel, 1);

		float dummy;
		stream.iStr->read(&dummy, 1);
		stream.iStr->read(&dummy, 1);
		if (stream.header.version > 0x4A) {
			stream.iStr->read(&dummy, 1);
		}
	}
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

const RE::NiColor& BSLightingShaderMaterialPBR::GetCoatColor() const
{
	return specularColor;
}

float BSLightingShaderMaterialPBR::GetCoatStrength() const
{
	return subSurfaceLightRolloff;
}

float BSLightingShaderMaterialPBR::GetCoatRoughness() const
{
	return coatRoughness;
}

float BSLightingShaderMaterialPBR::GetCoatSpecularLevel() const
{
	return coatSpecularLevel;
}