#include "Hooks.h"

#include <detours/Detours.h>

#include "Bindings.h"
#include "BSLightingShaderMaterialPBR.h"
#include "BSLightingShaderMaterialPBRLandscape.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"

#include "ShaderTools/BSShaderHooks.h"

namespace RE
{
	class BSLeafAnimNode
	{
	public:
		uint64_t parents[43];

		float unk158;
		float leafAmplitude;
		float leafFrequency;
		float unk164;
		float unk168;
	};
	static_assert(sizeof(BSLeafAnimNode) == 0x170);
}

std::unordered_map<void*, std::pair<std::unique_ptr<uint8_t[]>, size_t>> ShaderBytecodeMap;

void RegisterShaderBytecode(void* Shader, const void* Bytecode, size_t BytecodeLength)
{
	// Grab a copy since the pointer isn't going to be valid forever
	auto codeCopy = std::make_unique<uint8_t[]>(BytecodeLength);
	memcpy(codeCopy.get(), Bytecode, BytecodeLength);
	logger::debug(fmt::runtime("Saving shader at index {:x} with {} bytes:\t{:x}"), (std::uintptr_t)Shader, BytecodeLength, (std::uintptr_t)Bytecode);
	ShaderBytecodeMap.emplace(Shader, std::make_pair(std::move(codeCopy), BytecodeLength));
}

const std::pair<std::unique_ptr<uint8_t[]>, size_t>& GetShaderBytecode(void* Shader)
{
	logger::debug(fmt::runtime("Loading shader at index {:x}"), (std::uintptr_t)Shader);
	return ShaderBytecodeMap.at(Shader);
}

void DumpShader(const REX::BSShader* thisClass, const RE::BSGraphics::VertexShader* shader, const std::pair<std::unique_ptr<uint8_t[]>, size_t>& bytecode)
{
	uint8_t* dxbcData = new uint8_t[bytecode.second];
	size_t dxbcLen = bytecode.second;
	memcpy(dxbcData, bytecode.first.get(), bytecode.second);

	std::string dumpDir = std::format("Data\\ShaderDump\\{}\\{}.vs.bin", thisClass->m_LoaderType, shader->id);
	auto directoryPath = std::format("Data\\ShaderDump\\{}", thisClass->m_LoaderType);
	logger::debug(fmt::runtime("Dumping vertex shader {} with id {:x} at {}"), thisClass->m_LoaderType, shader->id, dumpDir);

	if (!std::filesystem::is_directory(directoryPath)) {
		try {
			std::filesystem::create_directories(directoryPath);
		} catch (std::filesystem::filesystem_error const& ex) {
			logger::error("Failed to create folder: {}", ex.what());
		}
	}

	if (FILE * file; fopen_s(&file, dumpDir.c_str(), "wb") == 0) {
		fwrite(dxbcData, 1, dxbcLen, file);
		fclose(file);
	}

	delete[] dxbcData;
}

void DumpShader(const REX::BSShader* thisClass, const RE::BSGraphics::PixelShader* shader, const std::pair<std::unique_ptr<uint8_t[]>, size_t>& bytecode)
{
	uint8_t* dxbcData = new uint8_t[bytecode.second];
	size_t dxbcLen = bytecode.second;
	memcpy(dxbcData, bytecode.first.get(), bytecode.second);

	std::string dumpDir = std::format("Data\\ShaderDump\\{}\\{:X}.ps.bin", thisClass->m_LoaderType, shader->id);

	auto directoryPath = std::format("Data\\ShaderDump\\{}", thisClass->m_LoaderType);
	logger::debug(fmt::runtime("Dumping pixel shader {} with id {:x} at {}"), thisClass->m_LoaderType, shader->id, dumpDir);
	if (!std::filesystem::is_directory(directoryPath)) {
		try {
			std::filesystem::create_directories(directoryPath);
		} catch (std::filesystem::filesystem_error const& ex) {
			logger::error("Failed to create folder: {}", ex.what());
		}
	}

	if (FILE * file; fopen_s(&file, dumpDir.c_str(), "wb") == 0) {
		fwrite(dxbcData, 1, dxbcLen, file);
		fclose(file);
	}

	delete[] dxbcData;
}

void hk_BSShader_LoadShaders(RE::BSShader* shader, std::uintptr_t stream);

decltype(&hk_BSShader_LoadShaders) ptr_BSShader_LoadShaders;

namespace Permutations
{
	template <typename RangeType>
	std::unordered_set<uint32_t> GenerateFlagPermutations(const RangeType& flags, uint32_t constantFlags)
	{
		std::vector<uint32_t> flagValues;
		std::ranges::transform(flags, std::back_inserter(flagValues), [](auto flag) { return static_cast<uint32_t>(flag); });
		const uint32_t size = static_cast<uint32_t>(flagValues.size());

		std::unordered_set<uint32_t> result;
		for (uint32_t mask = 0; mask < (1u << size); ++mask) {
			uint32_t flag = constantFlags;
			for (size_t index = 0; index < size; ++index) {
				if (mask & (1 << index)) {
					flag |= flagValues[index];
				}
			}
			result.insert(flag);
		}

		return result;
	}

	uint32_t GetLightingShaderDescriptor(SIE::ShaderCache::LightingShaderTechniques technique, uint32_t flags)
	{
		return ((static_cast<uint32_t>(technique) & 0x3F) << 24) | flags;
	}

	void AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques technique, const std::unordered_set<uint32_t>& flags, std::unordered_set<uint32_t>& result)
	{
		for (uint32_t flag : flags) {
			result.insert(GetLightingShaderDescriptor(technique, flag));
		}
	}

	std::unordered_set<uint32_t> GeneratePBRLightingVertexPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ VC, Skinned, WorldMap };
		constexpr std::array projectedUvFlags{ VC, WorldMap };
		constexpr std::array treeFlags{ VC, Skinned };
		constexpr std::array landFlags{ VC };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(TruePbr);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = GenerateFlagPermutations(landFlags, defaultConstantFlags);

		std::unordered_set<uint32_t> result;
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, defaultFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, projectedUvFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::TreeAnim, treeFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLand, landFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend, landFlagValues, result);
		return result;
	}

	std::unordered_set<uint32_t> GeneratePBRLightingPixelPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ Skinned, DoAlphaTest, AdditionalAlphaMask };
		constexpr std::array projectedUvFlags{ DoAlphaTest, AdditionalAlphaMask, Snow, BaseObjectIsSnow };
		constexpr std::array lodObjectsFlags{ WorldMap, DoAlphaTest, AdditionalAlphaMask, ProjectedUV };
		constexpr std::array treeFlags{ Skinned, DoAlphaTest, AdditionalAlphaMask };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> lodObjectsFlagValues = GenerateFlagPermutations(lodObjectsFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = { defaultConstantFlags };

		std::unordered_set<uint32_t> result;
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, defaultFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, projectedUvFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjects, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjectHD, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::TreeAnim, treeFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLand, landFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend, landFlagValues, result);
		return result;
	}

	std::unordered_set<uint32_t> GeneratePBRGrassPermutations()
	{
		using enum SIE::ShaderCache::GrassShaderTechniques;
		using enum SIE::ShaderCache::GrassShaderFlags;

		return { static_cast<uint32_t>(TruePbr),
			static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(AlphaTest) };
	}

	std::unordered_set<uint32_t> GeneratePBRGrassVertexPermutations()
	{
		return GeneratePBRGrassPermutations();
	}

	std::unordered_set<uint32_t> GeneratePBRGrassPixelPermutations()
	{
		return GeneratePBRGrassPermutations();
	}
}

void hk_BSShader_LoadShaders(RE::BSShader* shader, std::uintptr_t stream)
{
	(ptr_BSShader_LoadShaders)(shader, stream);
	auto& shaderCache = SIE::ShaderCache::Instance();

	if (shaderCache.IsDiskCache() || shaderCache.IsDump()) {
		if (shaderCache.IsDiskCache() && shader->shaderType == RE::BSShader::Type::Lighting) {
			const auto vertexPermutations = Permutations::GeneratePBRLightingVertexPermutations();
			for (auto descriptor : vertexPermutations) {
				auto vertexShaderDesriptor = descriptor;
				auto pixelShaderDescriptor = descriptor;
				State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				std::ignore = shaderCache.GetVertexShader(*shader, vertexShaderDesriptor);
			}

			const auto pixelPermutations = Permutations::GeneratePBRLightingPixelPermutations();
			for (auto descriptor : pixelPermutations) {
				auto vertexShaderDesriptor = descriptor;
				auto pixelShaderDescriptor = descriptor;
				State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				std::ignore = shaderCache.GetPixelShader(*shader, pixelShaderDescriptor);
			}
		}

		if (shaderCache.IsDiskCache() && shader->shaderType == RE::BSShader::Type::Grass) {
			const auto vertexPermutations = Permutations::GeneratePBRGrassVertexPermutations();
			for (auto descriptor : vertexPermutations) {
				auto vertexShaderDesriptor = descriptor;
				auto pixelShaderDescriptor = descriptor;
				State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				std::ignore = shaderCache.GetVertexShader(*shader, vertexShaderDesriptor);
			}

			const auto pixelPermutations = Permutations::GeneratePBRGrassPixelPermutations();
			for (auto descriptor : pixelPermutations) {
				auto vertexShaderDesriptor = descriptor;
				auto pixelShaderDescriptor = descriptor;
				State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
				std::ignore = shaderCache.GetPixelShader(*shader, pixelShaderDescriptor);
			}
		}

		for (const auto& entry : shader->vertexShaders) {
			if (entry->shader && shaderCache.IsDump()) {
				auto& bytecode = GetShaderBytecode(entry->shader);
				DumpShader((REX::BSShader*)shader, entry, bytecode);
			}
			auto vertexShaderDesriptor = entry->id;
			auto pixelShaderDescriptor = entry->id;
			State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			shaderCache.GetVertexShader(*shader, vertexShaderDesriptor);
		}
		for (const auto& entry : shader->pixelShaders) {
			if (entry->shader && shaderCache.IsDump()) {
				auto& bytecode = GetShaderBytecode(entry->shader);
				DumpShader((REX::BSShader*)shader, entry, bytecode);
			}
			auto vertexShaderDesriptor = entry->id;
			auto pixelShaderDescriptor = entry->id;
			State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			shaderCache.GetPixelShader(*shader, pixelShaderDescriptor);
		}
	}
	BSShaderHooks::hk_LoadShaders((REX::BSShader*)shader, stream);
};

bool hk_BSShader_BeginTechnique(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader);

decltype(&hk_BSShader_BeginTechnique) ptr_BSShader_BeginTechnique;

bool hk_BSShader_BeginTechnique(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t pixelDescriptor, bool skipPixelShader)
{
	auto state = State::GetSingleton();

	if (state->isInstancedPass && shader->shaderType == RE::BSShader::Type::Lighting)
	{
		vertexDescriptor |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Instanced);
	}

	state->currentShader = shader;
	state->currentVertexDescriptor = vertexDescriptor;
	state->currentPixelDescriptor = pixelDescriptor;
	state->updateShader = true;
	const bool shaderFound = (ptr_BSShader_BeginTechnique)(shader, vertexDescriptor, pixelDescriptor, skipPixelShader);
	if (shaderFound) {
		return shaderFound;
	}

	auto& shaderCache = SIE::ShaderCache::Instance();
	state->ModifyShaderLookup(*shader, vertexDescriptor, pixelDescriptor);
	RE::BSGraphics::VertexShader* vertexShader = shaderCache.GetVertexShader(*shader, vertexDescriptor);
	RE::BSGraphics::PixelShader* pixelShader = shaderCache.GetPixelShader(*shader, pixelDescriptor);
	if (vertexShader == nullptr || pixelShader == nullptr) {
		return false;
	}
	RE::BSGraphics::RendererShadowState::GetSingleton()->SetVertexShader(vertexShader);
	if (skipPixelShader) {
		pixelShader = nullptr;
	}
	RE::BSGraphics::RendererShadowState::GetSingleton()->SetPixelShader(pixelShader);
	state->isShaderSet = false;
	return true;
}

decltype(&IDXGISwapChain::Present) ptr_IDXGISwapChain_Present;

HRESULT WINAPI hk_IDXGISwapChain_Present(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
{
	State::GetSingleton()->Reset();
	Menu::GetSingleton()->DrawOverlay();
	return (This->*ptr_IDXGISwapChain_Present)(SyncInterval, Flags);
}

struct ExtendedRendererState
{
	static constexpr uint32_t NumPSTextures = 6;
	static constexpr uint32_t FirstPSTexture = 24;

	uint32_t PSResourceModifiedBits = 0;
	ID3D11ShaderResourceView* PSTexture[NumPSTextures];

	void SetPSTexture(size_t textureIndex, RE::BSGraphics::Texture* newTexture)
	{
		ID3D11ShaderResourceView* resourceView = newTexture ? newTexture->resourceView : nullptr;
		//if (PSTexture[textureIndex] != resourceView) 
		{
			PSTexture[textureIndex] = resourceView;
			PSResourceModifiedBits |= (1 << textureIndex);
		}
	}

	ExtendedRendererState()
	{
		std::fill_n(PSTexture, NumPSTextures, nullptr);
	}
} extendedRendererState;

void hk_BSGraphics_SetDirtyStates(bool isCompute);

decltype(&hk_BSGraphics_SetDirtyStates) ptr_BSGraphics_SetDirtyStates;

void hk_BSGraphics_SetDirtyStates(bool isCompute)
{
	//auto& shaderCache = SIE::ShaderCache::Instance();

	//if (shaderCache.IsEnabled())
	//	Bindings::GetSingleton()->SetDirtyStates(isCompute);

	(ptr_BSGraphics_SetDirtyStates)(isCompute);

	{
		auto context = State::GetSingleton()->context;
		for (uint32_t textureIndex = 0; textureIndex < ExtendedRendererState::NumPSTextures; ++textureIndex) {
			if (extendedRendererState.PSResourceModifiedBits & (1 << textureIndex)) {
				context->PSSetShaderResources(ExtendedRendererState::FirstPSTexture + textureIndex, 1, &extendedRendererState.PSTexture[textureIndex]);
			}
		}
		extendedRendererState.PSResourceModifiedBits = 0;
	}

	State::GetSingleton()->Draw();
}

decltype(&ID3D11Device::CreateVertexShader) ptrCreateVertexShader;
decltype(&ID3D11Device::CreatePixelShader) ptrCreatePixelShader;

HRESULT hk_CreateVertexShader(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader)
{
	HRESULT hr = (This->*ptrCreateVertexShader)(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);

	if (SUCCEEDED(hr))
		RegisterShaderBytecode(*ppVertexShader, pShaderBytecode, BytecodeLength);

	return hr;
}

HRESULT STDMETHODCALLTYPE hk_CreatePixelShader(ID3D11Device* This, const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader)
{
	HRESULT hr = (This->*ptrCreatePixelShader)(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);

	if (SUCCEEDED(hr))
		RegisterShaderBytecode(*ppPixelShader, pShaderBytecode, BytecodeLength);

	return hr;
}

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	[[maybe_unused]] D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	logger::info("Upgrading D3D11 feature level to 11.1");

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;  // Create a device with only the latest feature level

#ifndef NDEBUG
	Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	HRESULT hr = (*ptrD3D11CreateDeviceAndSwapChain)(
		pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		nullptr,
		ppImmediateContext);

	return hr;
}

void hk_BSShaderRenderTargets_Create();

decltype(&hk_BSShaderRenderTargets_Create) ptr_BSShaderRenderTargets_Create;

void hk_BSShaderRenderTargets_Create()
{
	logger::info("bUse64bitsHDRRenderTarget is {}", RE::GetINISetting("bUse64bitsHDRRenderTarget:Display")->data.b ? "enabled" : "disabled");
	(ptr_BSShaderRenderTargets_Create)();
	State::GetSingleton()->Setup();
}

static void hk_PollInputDevices(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events);
static inline REL::Relocation<decltype(hk_PollInputDevices)> _InputFunc;

void hk_PollInputDevices(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
{
	bool blockedDevice = true;
	auto menu = Menu::GetSingleton();

	if (a_events) {
		menu->ProcessInputEvents(a_events);

		if (*a_events) {
			if (auto device = (*a_events)->GetDevice()) {
				// Check that the device is not a Gamepad or VR controller. If it is, unblock input.
				bool vrDevice = false;
#ifdef ENABLE_SKYRIM_VR
				vrDevice = (REL::Module::IsVR() && ((device == RE::INPUT_DEVICES::INPUT_DEVICE::kVivePrimary) ||
															(device == RE::INPUT_DEVICES::INPUT_DEVICE::kViveSecondary) ||
															(device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusPrimary) ||
															(device == RE::INPUT_DEVICES::INPUT_DEVICE::kOculusSecondary) ||
															(device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRPrimary) ||
															(device == RE::INPUT_DEVICES::INPUT_DEVICE::kWMRSecondary)));
#endif
				blockedDevice = !((device == RE::INPUT_DEVICES::INPUT_DEVICE::kGamepad) || vrDevice);
			}
		}
	}

	if (blockedDevice && menu->ShouldSwallowInput()) {  //the menu is open, eat all keypresses
		constexpr RE::InputEvent* const dummy[] = { nullptr };
		_InputFunc(a_dispatcher, dummy);
		return;
	}

	_InputFunc(a_dispatcher, a_events);
}

namespace Hooks
{
	struct BSGraphics_Renderer_Init_InitD3D
	{
		static void thunk()
		{
			logger::info("Calling original Init3D");

			func();

			logger::info("Accessing render device information");

			auto manager = RE::BSGraphics::Renderer::GetSingleton();

			auto context = reinterpret_cast<ID3D11DeviceContext*>(manager->GetRuntimeData().context);
			auto swapchain = reinterpret_cast<IDXGISwapChain*>(manager->GetRuntimeData().renderWindows->swapChain);
			auto device = reinterpret_cast<ID3D11Device*>(manager->GetRuntimeData().forwarder);

			logger::info("Detouring virtual function tables");

			*(uintptr_t*)&ptr_IDXGISwapChain_Present = Detours::X64::DetourClassVTable(*(uintptr_t*)swapchain, &hk_IDXGISwapChain_Present, 8);

			auto& shaderCache = SIE::ShaderCache::Instance();
			if (shaderCache.IsDump()) {
				*(uintptr_t*)&ptrCreateVertexShader = Detours::X64::DetourClassVTable(*(uintptr_t*)device, &hk_CreateVertexShader, 12);
				*(uintptr_t*)&ptrCreatePixelShader = Detours::X64::DetourClassVTable(*(uintptr_t*)device, &hk_CreatePixelShader, 15);
			}
			Menu::GetSingleton()->Init(swapchain, device, context);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderISSAOCompositeSAO_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			State::GetSingleton()->DrawDeferred();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderISSAOCompositeFog_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			State::GetSingleton()->DrawDeferred();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderISSAOCompositeSAOFog_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			State::GetSingleton()->DrawDeferred();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShaderProperty_LoadBinary
	{
		static void thunk(RE::BSLightingShaderProperty* property, RE::NiStream& stream)
		{
			using enum RE::BSShaderProperty::EShaderPropertyFlag;

			RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kDefault;
			stream.iStr->read(&feature, 1);

			{
				auto vtable = REL::Relocation<void***>(RE::NiShadeProperty::VTABLE[0]);
				auto baseMethod = reinterpret_cast<void (*)(RE::NiShadeProperty*, RE::NiStream&)>((vtable.get()[0x18]));
				baseMethod(property, stream);
			}

			stream.iStr->read(&property->flags, 1);

			bool isPbr = false;
			{
				RE::BSLightingShaderMaterialBase* material = nullptr;
				if (property->flags.any(kMenuScreen)) {
					material = BSLightingShaderMaterialPBR::Make();
					isPbr = true;
				} else {
					material = RE::BSLightingShaderMaterialBase::CreateMaterial(feature);
				}
				property->LinkMaterial(nullptr, false);
				property->material = material;
			}

			{
				stream.iStr->read(&property->material->texCoordOffset[0].x, 1);
				stream.iStr->read(&property->material->texCoordOffset[0].y, 1);
				stream.iStr->read(&property->material->texCoordScale[0].x, 1);
				stream.iStr->read(&property->material->texCoordScale[0].y, 1);

				property->material->texCoordOffset[1] = property->material->texCoordOffset[0];
				property->material->texCoordScale[1] = property->material->texCoordScale[0];
			}

			stream.LoadLinkID();

			{
				RE::NiColor emissiveColor{};
				stream.iStr->read(&emissiveColor.red, 1);
				stream.iStr->read(&emissiveColor.green, 1);
				stream.iStr->read(&emissiveColor.blue, 1);

				if (property->emissiveColor != nullptr && property->flags.any(kOwnEmit)) {
					*property->emissiveColor = emissiveColor;
				}
			}

			stream.iStr->read(&property->emissiveMult, 1);

			static_cast<RE::BSLightingShaderMaterialBase*>(property->material)->LoadBinary(stream);

			if (isPbr)
			{
				auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(property->material);
				if (property->flags.any(kRimLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Subsurface);
				}
				property->flags.set(kVertexLighting);
				property->flags.reset(kMenuScreen, kSpecular, kGlowMap, kEnvMap, kSoftLighting, kRimLighting, kBackLighting, kAnisotropicLighting);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShaderProperty_GetRenderPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
		{
			auto renderPasses = func(property, geometry, renderFlags, accumulator);
			if (renderPasses == nullptr)
			{
				return renderPasses;
			}

			bool isPbr = false;

			if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting) && (property->material->GetFeature() == RE::BSShaderMaterial::Feature::kDefault || property->material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend))
			{
				isPbr = true;
			}

			auto currentPass = renderPasses->head;
			while (currentPass != nullptr) {
				if (currentPass->shader->shaderType == RE::BSShader::Type::Lighting) {
					constexpr uint32_t LightingTechniqueStart = 0x4800002D;
					auto lightingTechnique = currentPass->passEnum - LightingTechniqueStart;
					auto lightingFlags = lightingTechnique & ~(~0u << 24);
					auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((lightingTechnique >> 24) & 0x3F);
					lightingFlags &= ~0b111000u;
					if (isPbr) {
						lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr);
					}
					lightingTechnique = (static_cast<uint32_t>(lightingType) << 24) | lightingFlags;
					currentPass->passEnum = lightingTechnique + LightingTechniqueStart;
				}
				currentPass = currentPass->next;
			}

			return renderPasses;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_SetupMaterial
	{
		static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
		{
			using enum SIE::ShaderCache::LightingShaderTechniques;

			auto lightingFlags = shader->currentRawTechnique & ~(~0u << 24);
			auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((shader->currentRawTechnique >> 24) & 0x3F);
			if (!(lightingType == LODLand || lightingType == LODLandNoise) && (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr))) {
				auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
				auto renderer = RE::BSGraphics::Renderer::GetSingleton();

				RE::BSGraphics::Renderer::PrepareVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
				RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
				
				if (lightingType == MTLand || lightingType == MTLandLODBlend) {
					auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBRLandscape*>(material);

					constexpr size_t NormalStartIndex = 7;

					if (pbrMaterial->diffuseTexture != nullptr) {
						shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
					}
					if (pbrMaterial->normalTexture != nullptr) {
						shadowState->SetPSTexture(NormalStartIndex, pbrMaterial->normalTexture->rendererTexture);
					}
					if (pbrMaterial->landscapeRMAOSTextures[0] != nullptr) {
						extendedRendererState.SetPSTexture(0, pbrMaterial->landscapeRMAOSTextures[0]->rendererTexture);
					}
					for (uint32_t textureIndex = 1; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
						if (pbrMaterial->landscapeBCDTextures[textureIndex - 1] != nullptr) {
							shadowState->SetPSTexture(textureIndex, pbrMaterial->landscapeBCDTextures[textureIndex - 1]->rendererTexture);
						}
						if (pbrMaterial->landscapeNormalTextures[textureIndex - 1] != nullptr) {
							shadowState->SetPSTexture(NormalStartIndex + textureIndex, pbrMaterial->landscapeNormalTextures[textureIndex - 1]->rendererTexture);
						}
						if (pbrMaterial->landscapeRMAOSTextures[textureIndex] != nullptr) {
							extendedRendererState.SetPSTexture(textureIndex, pbrMaterial->landscapeRMAOSTextures[textureIndex]->rendererTexture);
						}
					}

					shadowState->SetPSTextureAddressMode(0, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
					shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					if (pbrMaterial->terrainOverlayTexture != nullptr) {
						shadowState->SetPSTexture(13, pbrMaterial->terrainOverlayTexture->rendererTexture);
						shadowState->SetPSTextureAddressMode(13, RE::BSGraphics::TextureAddressMode::kClampSClampT);
						shadowState->SetPSTextureFilterMode(13, RE::BSGraphics::TextureFilterMode::kAnisotropic);
					}

					if (pbrMaterial->terrainNoiseTexture != nullptr) {
						shadowState->SetPSTexture(15, pbrMaterial->terrainNoiseTexture->rendererTexture);
						shadowState->SetPSTextureAddressMode(15, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
						shadowState->SetPSTextureFilterMode(15, RE::BSGraphics::TextureFilterMode::kBilinear);
					}

					{
						uint32_t flags = 0;
						for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex)
						{
							if (pbrMaterial->isPbr[textureIndex])
							{
								flags |= (1 << textureIndex);
							}
						}
						shadowState->SetPSConstant(flags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 36);
					}

					{
						constexpr size_t PBRParamsStartIndex = 37;

						for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
							std::array<float, 3> PBRParams;
							PBRParams[0] = pbrMaterial->roughnessScales[textureIndex];
							PBRParams[1] = pbrMaterial->displacementScales[textureIndex];
							PBRParams[2] = pbrMaterial->specularLevels[textureIndex];
							shadowState->SetPSConstant(PBRParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, PBRParamsStartIndex + textureIndex);
						}
					}

					{
						std::array<float, 4> lodTexParams;
						lodTexParams[0] = pbrMaterial->terrainTexOffsetX;
						lodTexParams[1] = pbrMaterial->terrainTexOffsetY;
						lodTexParams[2] = 1.f;
						lodTexParams[3] = pbrMaterial->terrainTexFade;
						shadowState->SetPSConstant(lodTexParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 24);
					}
				} 
				else if (lightingType == None || lightingType == TreeAnim) {
					auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
					if (pbrMaterial->diffuseRenderTargetSourceIndex != -1) {
						shadowState->SetPSTexture(0, renderer->GetRuntimeData().renderTargets[pbrMaterial->diffuseRenderTargetSourceIndex]);
					}
					else {
						shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
					}
					shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shadowState->SetPSTexture(1, pbrMaterial->normalTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(1, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shadowState->SetPSTexture(5, pbrMaterial->rmaosTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(5, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(5, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					stl::enumeration<PBRShaderFlags> shaderFlags;
					if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
						shaderFlags.set(PBRShaderFlags::Subsurface);
					}

					const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
					if (hasEmissive) {
						shadowState->SetPSTexture(6, pbrMaterial->emissiveTexture->rendererTexture);
						shadowState->SetPSTextureAddressMode(6, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
						shadowState->SetPSTextureFilterMode(6, RE::BSGraphics::TextureFilterMode::kAnisotropic);

						shaderFlags.set(PBRShaderFlags::HasEmissive);
					}

					const bool hasDisplacement = pbrMaterial->displacementTexture != nullptr && pbrMaterial->displacementTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
					if (hasDisplacement) {
						shadowState->SetPSTexture(3, pbrMaterial->displacementTexture->rendererTexture);
						shadowState->SetPSTextureAddressMode(3, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
						shadowState->SetPSTextureFilterMode(3, RE::BSGraphics::TextureFilterMode::kAnisotropic);

						shaderFlags.set(PBRShaderFlags::HasDisplacement);
					}

					const bool hasSubsurface = pbrMaterial->subsurfaceTexture != nullptr && pbrMaterial->subsurfaceTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureWhite;
					if (hasSubsurface) {
						shadowState->SetPSTexture(12, pbrMaterial->subsurfaceTexture->rendererTexture);
						shadowState->SetPSTextureAddressMode(12, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
						shadowState->SetPSTextureFilterMode(12, RE::BSGraphics::TextureFilterMode::kAnisotropic);

						shaderFlags.set(PBRShaderFlags::HasSubsurface);
					}

					{
						shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 36);
					}

					{
						std::array<float, 3> PBRParams1;
						PBRParams1[0] = pbrMaterial->GetRoughnessScale();
						PBRParams1[1] = pbrMaterial->GetDisplacementScale();
						PBRParams1[2] = pbrMaterial->GetSpecularLevel();
						shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 37);
					}

					{
						std::array<float, 4> PBRParams2;
						PBRParams2[0] = pbrMaterial->GetSubsurfaceColor().red;
						PBRParams2[1] = pbrMaterial->GetSubsurfaceColor().green;
						PBRParams2[2] = pbrMaterial->GetSubsurfaceColor().blue;
						PBRParams2[3] = pbrMaterial->GetSubsurfaceOpacity();
						shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 43);
					}
				}

				{
					const uint32_t bufferIndex = RE::BSShaderManager::State::GetSingleton().textureTransformCurrentBuffer;

					std::array<float, 4> texCoordOffsetScale;
					texCoordOffsetScale[0] = material->texCoordOffset[bufferIndex].x;
					texCoordOffsetScale[1] = material->texCoordOffset[bufferIndex].y;
					texCoordOffsetScale[2] = material->texCoordScale[bufferIndex].x;
					texCoordOffsetScale[3] = material->texCoordScale[bufferIndex].y;
					shadowState->SetVSConstant(texCoordOffsetScale, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 11);
				}

				if (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::CharacterLight)) {
					static const REL::Relocation<RE::ImageSpaceTexture*> characterLightTexture{ RELOCATION_ID(513464, 391302) };

					if (characterLightTexture->renderTarget >= RE::RENDER_TARGET::kFRAMEBUFFER) {
						shadowState->SetPSTexture(11, renderer->GetRuntimeData().renderTargets[characterLightTexture->renderTarget]);
						shadowState->SetPSTextureAddressMode(11, RE::BSGraphics::TextureAddressMode::kClampSClampT);
					}

					const auto& smState = RE::BSShaderManager::State::GetSingleton();
					std::array<float, 4> characterLightParams;
					if (smState.characterLightEnabled) {
						std::copy_n(smState.characterLightParams, 4, characterLightParams.data());
					}
					else
					{
						std::fill_n(characterLightParams.data(), 4, 0.f);
					}
					shadowState->SetPSConstant(characterLightParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 35);
				}

				RE::BSGraphics::Renderer::FlushVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
				RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
				RE::BSGraphics::Renderer::ApplyVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
				RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			}
			else {
				func(shader, material);
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_SetupGeometry
	{
		static void thunk(RE::BSLightingShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
		{
			const uint32_t originalExtraFlags = shader->currentRawTechnique & 0b111000u;

			if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
				shader->currentRawTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
			}

			shader->currentRawTechnique &= ~0b111000u;
			shader->currentRawTechnique |= ((pass->numLights - 1) << 3);

			func(shader, pass, renderFlags);

			shader->currentRawTechnique &= ~0b111000u;
			shader->currentRawTechnique |= originalExtraFlags;

			if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
				shader->currentRawTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	uint32_t hk_BSLightingShader_GetPixelTechnique(uint32_t rawTechnique)
	{
		uint32_t pixelTechnique = rawTechnique;

		pixelTechnique &= ~0b111000000u;
		if ((pixelTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ModelSpaceNormals)) == 0) 
		{
			pixelTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned);
		}
		pixelTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::VC);

		return pixelTechnique;
	}

	RE::BSLightingShaderMaterialBase* hk_BSLightingShaderMaterialBase_CreateMaterial(RE::BSShaderMaterial::Feature feature)
	{
		if (feature == BSLightingShaderMaterialPBR::FEATURE) {
			return BSLightingShaderMaterialPBR::Make();
		}
		else if (feature == BSLightingShaderMaterialPBRLandscape::FEATURE) {
			return BSLightingShaderMaterialPBRLandscape::Make();
		}
		return RE::BSLightingShaderMaterialBase::CreateMaterial(feature);
	}

	void SetupLandscapeTexture(BSLightingShaderMaterialPBRLandscape& material, RE::TESLandTexture& landTexture, uint32_t textureIndex)
	{
		if (textureIndex >= 6)
		{
			return;
		}

		auto textureSet = landTexture.textureSet;
		if (textureSet == nullptr)
		{
			return;
		}

		auto* textureSetData = State::GetSingleton()->GetPBRTextureSetData(landTexture.textureSet);
		const bool isPbr = textureSetData != nullptr;

		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::BcdTexture, textureIndex == 0 ? material.diffuseTexture : material.landscapeBCDTextures[textureIndex - 1]);
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::NormalTexture, textureIndex == 0 ? material.normalTexture : material.landscapeNormalTextures[textureIndex - 1]);

		if (isPbr) {
			textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::RmaosTexture, material.landscapeRMAOSTextures[textureIndex]);
			material.displacementScales[textureIndex] = textureSetData->displacementScale;
			material.roughnessScales[textureIndex] = textureSetData->roughnessScale;
			material.specularLevels[textureIndex] = textureSetData->specularLevel;
		}
		material.isPbr[textureIndex] = isPbr;

		if (textureIndex == 0) {
			if (material.diffuseTexture != nullptr) {
				material.numLandscapeTextures = std::max(material.numLandscapeTextures, 1u);
			}
		} else {
			if (material.landscapeBCDTextures[textureIndex] != nullptr) {
				material.numLandscapeTextures = std::max(material.numLandscapeTextures, textureIndex + 2);
			}
		}
	}

	bool hk_TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	decltype(&hk_TESObjectLAND_SetupMaterial) ptr_TESObjectLAND_SetupMaterial;

	bool hk_TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
	{
		auto* state = State::GetSingleton();

		bool isPbr = false;
		if (land->loadedData != nullptr) {
			for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
				if (land->loadedData->defQuadTextures[quadIndex] != nullptr) {
					if (state->IsPBRTextureSet(land->loadedData->defQuadTextures[quadIndex]->textureSet)) {
						isPbr = true;
						break;
					}
				}
				for (uint32_t textureIndex = 0; textureIndex < 6; ++textureIndex) {
					if (land->loadedData->quadTextures[quadIndex][textureIndex] != nullptr) {
						if (state->IsPBRTextureSet(land->loadedData->quadTextures[quadIndex][textureIndex]->textureSet)) {
							isPbr = true;
							break;
						}
					}
				}
			}
		}

		if (!isPbr) {
			return ptr_TESObjectLAND_SetupMaterial(land);
		}

		static const auto settings = RE::INISettingCollection::GetSingleton();
		static const bool bEnableLandFade = settings->GetSetting("bEnableLandFade:Display");
		static const bool bDrawLandShadows = settings->GetSetting("bDrawLandShadows:Display");
		static const bool bLandSpecular = settings->GetSetting("bLandSpecular:Landscape");

		if (land->loadedData != nullptr && land->loadedData->mesh[0] != nullptr) {
			land->data.flags.set(static_cast<RE::OBJ_LAND::Flag>(8));
			for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
				auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(RE::MemoryManager::GetSingleton()->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
				shaderProperty->Ctor();

				{
					BSLightingShaderMaterialPBRLandscape srcMaterial;
					shaderProperty->LinkMaterial(&srcMaterial, true);
				}

				auto material = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderProperty->material);
				const auto& stateData = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();

				material->diffuseTexture = stateData.defaultTextureBlack;
				material->normalTexture = stateData.defaultTextureNormalMap;
				material->landscapeRMAOSTextures[0] = stateData.defaultTextureWhite;
				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles - 1; ++textureIndex) {
					material->landscapeBCDTextures[textureIndex] = stateData.defaultTextureBlack;
					material->landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
					material->landscapeRMAOSTextures[textureIndex + 1] = stateData.defaultTextureWhite;
				}

				if (auto defTexture = land->loadedData->defQuadTextures[quadIndex]) {
					SetupLandscapeTexture(*material, *defTexture, 0);
				}
				for (uint32_t textureIndex = 0; textureIndex < 6; ++textureIndex) {
					if (auto landTexture = land->loadedData->quadTextures[quadIndex][textureIndex]) {
						SetupLandscapeTexture(*material, *landTexture, textureIndex + 1);
					}
				}

				if (bEnableLandFade) {
					shaderProperty->unk108 = false;
				}

				bool noLODLandBlend = false;
				auto tes = RE::TES::GetSingleton();
				if (tes->worldSpace != nullptr) 
				{
					if (auto terrainManager = tes->worldSpace->GetTerrainManager())
					{
						noLODLandBlend = reinterpret_cast<bool*>(terrainManager)[0x36];
					}
				}
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMultiTextureLandscape, true);
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kReceiveShadows, true);
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kCastShadows, bDrawLandShadows);
				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kNoLODLandBlend, noLODLandBlend);

				shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kVertexLighting, true);

				const auto& children = land->loadedData->mesh[quadIndex]->GetChildren();
				auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
				shaderProperty->SetupGeometry(geometry);
				if (geometry != nullptr)
				{
					geometry->GetGeometryRuntimeData().properties[1] = RE::NiPointer(shaderProperty);
				}

				RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]->AttachObject(geometry);
			}

			return true;
		}

		return false;
	}

	std::unordered_map<uint32_t, std::string> EditorIDs;
	struct TESForm_GetFormEditorID
	{
		static const char* thunk(const RE::TESForm* form)
		{
			auto it = EditorIDs.find(form->GetFormID());
			if (it == EditorIDs.cend()) {
				return "";
			}
			return it->second.c_str();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TESForm_SetFormEditorID
	{
		static bool thunk(RE::TESForm* form, const char* editorId)
		{
			EditorIDs[form->GetFormID()] = editorId;
			return true;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderHDRTonemapBlendCinematic_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			State::GetSingleton()->DrawPreProcess();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImagespaceShaderHDRTonemapBlendCinematicFade_SetupTechnique
	{
		static void thunk(RE::BSShader* a_shader, RE::BSShaderMaterial* a_material)
		{
			State::GetSingleton()->DrawPreProcess();
			func(a_shader, a_material);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct WndProcHandler_Hook
	{
		static LRESULT thunk(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			if (a_msg == WM_KILLFOCUS) {
				Menu::GetSingleton()->OnFocusLost();
				auto& io = ImGui::GetIO();
				io.ClearInputKeys();
				io.ClearEventsQueue();
			}
			return func(a_hwnd, a_msg, a_wParam, a_lParam);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct RegisterClassA_Hook
	{
		static ATOM thunk(WNDCLASSA* a_wndClass)
		{
			WndProcHandler_Hook::func = reinterpret_cast<uintptr_t>(a_wndClass->lpfnWndProc);
			a_wndClass->lpfnWndProc = &WndProcHandler_Hook::thunk;

			return func(a_wndClass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Main
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			State::GetSingleton()->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_Normals
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			State::GetSingleton()->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct CreateRenderTarget_NormalsSwap
	{
		static void thunk(RE::BSGraphics::Renderer* This, RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			State::GetSingleton()->ModifyRenderTarget(a_target, a_properties);
			func(This, a_target, a_properties);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void hk_SetPerFrameBuffers(void* renderer);
	decltype(&hk_SetPerFrameBuffers) ptr_SetPerFrameBuffers;

	void hk_SetPerFrameBuffers(void* renderer)
	{
		ptr_SetPerFrameBuffers(renderer);
		State::GetSingleton()->SetupFrame();
	}

	void hk_BSTempEffectSimpleDecal_SetupGeometry(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended);
	decltype(&hk_BSTempEffectSimpleDecal_SetupGeometry) ptr_BSTempEffectSimpleDecal_SetupGeometry;

	void hk_BSTempEffectSimpleDecal_SetupGeometry(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended)
	{
		ptr_BSTempEffectSimpleDecal_SetupGeometry(decal, geometry, textureSet, blended);

		if (auto* shaderProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get());
			shaderProperty != nullptr && State::GetSingleton()->IsPBRTextureSet(textureSet)) {
			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->LinkMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, textureSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;
				shaderProperty->SetFlags(kParallaxOcclusion, false);
				shaderProperty->SetFlags(kParallax, false);
				shaderProperty->SetFlags(kGlowMap, false);
				shaderProperty->SetFlags(kEnvMap, false);
				shaderProperty->SetFlags(kSpecular, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}
		}
	}

	struct BSTempEffectGeometryDecal_Initialize
	{
		static void thunk(RE::BSTempEffectGeometryDecal* decal)
		{
			func(decal);

			if (decal->decal != nullptr && State::GetSingleton()->IsPBRTextureSet(decal->texSet)) {
				auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(RE::MemoryManager::GetSingleton()->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
				shaderProperty->Ctor();

				{
					BSLightingShaderMaterialPBR srcMaterial;
					shaderProperty->LinkMaterial(&srcMaterial, true);
				}

				auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
				pbrMaterial->OnLoadTextureSet(0, decal->texSet);

				constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
				*shaderProperty->emissiveColor = whiteColor;
				const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
				shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

				{
					using enum RE::BSShaderProperty::EShaderPropertyFlag8;

					shaderProperty->SetFlags(kSkinned, true);
					shaderProperty->SetFlags(kDynamicDecal, true);
					shaderProperty->SetFlags(kZBufferTest, true);
					shaderProperty->SetFlags(kZBufferWrite, false);

					shaderProperty->SetFlags(kVertexLighting, true);
				}

				if (auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(decal->decal->GetGeometryRuntimeData().properties[0].get())) {
					alphaProperty->alphaFlags = (alphaProperty->alphaFlags & ~0x1FE) | 0xED;
				}

				shaderProperty->SetupGeometry(decal->decal.get());
				decal->decal->GetGeometryRuntimeData().properties[1] = RE::NiPointer(shaderProperty);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGrassShaderProperty_ctor
	{
		static RE::BSLightingShaderProperty* thunk(RE::BSLightingShaderProperty* property)
		{
			const uint64_t stackPointer = reinterpret_cast<uint64_t>(_AddressOfReturnAddress());
			const uint64_t lightingPropertyAddress = stackPointer + (REL::Module::IsAE() ? 0x68 : 0x70);
			auto* lightingProperty = *reinterpret_cast<RE::BSLightingShaderProperty**>(lightingPropertyAddress);

			RE::BSLightingShaderProperty* grassProperty = func(property);

			if (lightingProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
				if (auto* pbrSrcMaterial = static_cast<BSLightingShaderMaterialPBR*>(lightingProperty->material)) {
					BSLightingShaderMaterialPBR srcMaterial;
					grassProperty->LinkMaterial(&srcMaterial, true);

					grassProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMenuScreen, true);

					auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(grassProperty->material);
					pbrMaterial->pbrFlags = pbrSrcMaterial->pbrFlags;
					pbrMaterial->normalTexture = pbrSrcMaterial->normalTexture;
					pbrMaterial->rmaosTexture = pbrSrcMaterial->rmaosTexture;
					pbrMaterial->subsurfaceTexture = pbrSrcMaterial->subsurfaceTexture;
					pbrMaterial->specularColorScale = pbrSrcMaterial->specularColorScale;
					pbrMaterial->specularPower = pbrSrcMaterial->specularPower;
					pbrMaterial->specularColor = pbrSrcMaterial->specularColor;
					pbrMaterial->subSurfaceLightRolloff = pbrSrcMaterial->subSurfaceLightRolloff;
				}
			}

			return grassProperty;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGrassShaderProperty_GetRenderPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
		{
			auto renderPasses = func(property, geometry, renderFlags, accumulator);
			if (renderPasses == nullptr)
			{
				return renderPasses;
			}

			const bool isPbr = property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
			if (isPbr) {
				auto currentPass = renderPasses->head;
				while (currentPass != nullptr) {
					if (currentPass->shader->shaderType == RE::BSShader::Type::Grass && currentPass->passEnum != 0x5C00005C) {
						currentPass->passEnum = 0x5C000042;
					}
					currentPass = currentPass->next;
				}
			}

			return renderPasses;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGrassShader_SetupTechnique
	{
		static bool thunk(RE::BSShader* shader, uint32_t globalTechnique)
		{
			if (globalTechnique == 0x5C000042) {
				auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
				auto* graphicsState = RE::BSGraphics::State::GetSingleton();
				auto* renderer = RE::BSGraphics::Renderer::GetSingleton();

				const uint32_t localTechnique = static_cast<uint32_t>(SIE::ShaderCache::GrassShaderTechniques::TruePbr);
				uint32_t shaderDescriptor = localTechnique;
				if (graphicsState->useEarlyZ)
				{
					shaderDescriptor |= static_cast<uint32_t>(SIE::ShaderCache::GrassShaderFlags::AlphaTest);
				}

				const bool began = hk_BSShader_BeginTechnique(shader, shaderDescriptor, shaderDescriptor, false);
				if (!began)
				{
					return false;
				}

				static auto fogMethod = REL::Relocation<void (*)()>(REL::RelocationID(100000, 106707));
				fogMethod();

				static auto* bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
				if (!bShadowsOnGrass->GetBool()) {
					shadowState->SetPSTexture(1, graphicsState->defaultTextureWhite->rendererTexture);
					shadowState->SetPSTextureAddressMode(1, RE::BSGraphics::TextureAddressMode::kClampSClampT);
					shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kNearest);
				}
				else
				{
					shadowState->SetPSTexture(1, renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK]);
					shadowState->SetPSTextureAddressMode(1, RE::BSGraphics::TextureAddressMode::kClampSClampT);

					static auto* shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
					shadowState->SetPSTextureFilterMode(1, shadowMaskQuarter->GetSInt() != 4 ? RE::BSGraphics::TextureFilterMode::kBilinear : RE::BSGraphics::TextureFilterMode::kNearest);
				}

				return true;
			}

			return func(shader, globalTechnique);
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGrassShader_SetupMaterial
	{
		static void thunk(RE::BSShader* shader, RE::BSLightingShaderMaterialBase const* material)
		{
			const auto& state = State::GetSingleton();
			const auto technique = static_cast<SIE::ShaderCache::GrassShaderTechniques>(state->currentPixelDescriptor & 0b1111);
			
			if (technique == SIE::ShaderCache::GrassShaderTechniques::TruePbr) {
				auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();

				RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

				auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
				shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shadowState->SetPSTexture(2, pbrMaterial->normalTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(2, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(2, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shadowState->SetPSTexture(3, pbrMaterial->rmaosTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(3, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(3, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				stl::enumeration<PBRShaderFlags> shaderFlags;
				if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
					shaderFlags.set(PBRShaderFlags::Subsurface);
				}

				const bool hasSubsurface = pbrMaterial->subsurfaceTexture != nullptr && pbrMaterial->subsurfaceTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureWhite;
				if (hasSubsurface) {
					shadowState->SetPSTexture(4, pbrMaterial->subsurfaceTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(4, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(4, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasSubsurface);
				}

				{
					shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 0);
				}

				{
					std::array<float, 3> PBRParams1;
					PBRParams1[0] = pbrMaterial->GetRoughnessScale();
					PBRParams1[1] = pbrMaterial->GetSpecularLevel();
					shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 1);
				}

				{
					std::array<float, 4> PBRParams2;
					PBRParams2[0] = pbrMaterial->GetSubsurfaceColor().red;
					PBRParams2[1] = pbrMaterial->GetSubsurfaceColor().green;
					PBRParams2[2] = pbrMaterial->GetSubsurfaceColor().blue;
					PBRParams2[3] = pbrMaterial->GetSubsurfaceOpacity();
					shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 2);
				}

				RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
				RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			} else {
				func(shader, material);
			}
		};
		static inline REL::Relocation<decltype(thunk)> func;
	};

	winrt::com_ptr<ID3D11InputLayout> CreateInputLayout(const RE::BSGraphics::VertexDesc& vertexDesc)
	{
		using enum RE::BSGraphics::Vertex::Attribute;

		static auto* csState = State::GetSingleton();

		D3D11_INPUT_ELEMENT_DESC inputElements[32];
		UINT elementIndex = 0;

		auto addElement = [&vertexDesc, &inputElements, &elementIndex](RE::BSGraphics::Vertex::Attribute attribute, LPCSTR semanticName, UINT semanticIndex, DXGI_FORMAT format, UINT offset, D3D11_INPUT_CLASSIFICATION inputClass) {
			UINT inputSlot = std::numeric_limits<UINT>::max();
			if ((vertexDesc.GetFlags() & (1 << attribute)) != 0) {
				inputSlot = 0;
			} else if ((vertexDesc.GetFlags() & (1 << (attribute + VA_COUNT))) != 0) {
				inputSlot = 1;
			}
			if (attribute == VA_POSITION) {
				inputElements[elementIndex++] = { semanticName, semanticIndex, format, inputSlot, 0, inputClass, static_cast<UINT>(inputClass) };
			} else if (inputSlot != -1) {
				inputElements[elementIndex++] = { semanticName, semanticIndex, format, inputSlot, vertexDesc.GetAttributeOffset(attribute) + offset, inputClass, static_cast<UINT>(inputClass) };
			}
		};

		addElement(VA_POSITION, "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_TEXCOORD0, "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_TEXCOORD1, "TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_NORMAL, "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_BINORMAL, "BINORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_COLOR, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_SKINNING, "BLENDWEIGHT", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_SKINNING, "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 8, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_LANDDATA, "TEXCOORD", 2, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_LANDDATA, "TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_UNORM, 4, D3D11_INPUT_PER_VERTEX_DATA);
		addElement(VA_EYEDATA, "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_INPUT_PER_VERTEX_DATA);
		//addElement(VA_INSTANCEDATA, "TEXCOORD", 4, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, D3D11_INPUT_PER_INSTANCE_DATA);
		//addElement(VA_INSTANCEDATA, "TEXCOORD", 5, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, D3D11_INPUT_PER_INSTANCE_DATA);
		//addElement(VA_INSTANCEDATA, "TEXCOORD", 6, DXGI_FORMAT_R16G16B16A16_FLOAT, 16, D3D11_INPUT_PER_INSTANCE_DATA);
		//addElement(VA_INSTANCEDATA, "TEXCOORD", 7, DXGI_FORMAT_R16G16B16A16_FLOAT, 24, D3D11_INPUT_PER_INSTANCE_DATA);

		if (csState->instancedShaderType == RE::BSShader::Type::Utility) {
			inputElements[elementIndex++] = { "INSTANCEPOS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
		} else if (csState->instancedShaderType == RE::BSShader::Type::Lighting) {
			inputElements[elementIndex++] = { "INSTANCEPOS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
			inputElements[elementIndex++] = { "INSTANCEPOS", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1 };
		}

		winrt::com_ptr<ID3D11InputLayout> result;
		State::GetSingleton()->device->CreateInputLayout(inputElements, elementIndex, State::GetSingleton()->shadowState->GetRuntimeData().currentVertexShader->rawBytecode, State::GetSingleton()->shadowState->GetRuntimeData().currentVertexShader->byteCodeSize, result.put());
		return result;
	}

	DirectX::XMMATRIX GetXMFromNiPosAdjust(const RE::NiTransform& transform, const RE::NiPoint3& posAdjust)
	{
		DirectX::XMMATRIX temp;

		const RE::NiMatrix3& m = transform.rotate;
		const float scale = transform.scale;

		temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
									  m.entry[0][0],
									  m.entry[1][0],
									  m.entry[2][0],
									  0.0f),
			scale);

		temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
									  m.entry[0][1],
									  m.entry[1][1],
									  m.entry[2][1],
									  0.0f),
			scale);

		temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
									  m.entry[0][2],
									  m.entry[1][2],
									  m.entry[2][2],
									  0.0f),
			scale);

		temp.r[3] = DirectX::XMVectorSet(
			transform.translate.x - posAdjust.x,
			transform.translate.y - posAdjust.y,
			transform.translate.z - posAdjust.z,
			1.0f);

		return temp;
	}

	DirectX::XMMATRIX GetXMFromNi(const RE::NiTransform& transform)
	{
		return GetXMFromNiPosAdjust(transform, State::GetSingleton()->shadowState->GetRuntimeData().posAdjust.getEye(0));
	}

	void TransposeStoreMatrix3x4(float* Dest, const DirectX::XMMATRIX& Source)
	{
		DirectX::XMMATRIX transposed = DirectX::XMMatrixTranspose(Source);

		_mm_store_ps(&Dest[0], transposed.r[0]);
		_mm_store_ps(&Dest[4], transposed.r[1]);
		_mm_store_ps(&Dest[8], transposed.r[2]);
	}

	void GeometrySetupConstantWorld(float* data, const RE::NiTransform& transform, const RE::NiPoint3* posAdjust)
	{
		DirectX::XMMATRIX projMatrix;
		projMatrix = posAdjust ? GetXMFromNiPosAdjust(transform, *posAdjust) : GetXMFromNi(transform);
		TransposeStoreMatrix3x4(data, projMatrix);
	}

	float FastInvSqrt(float number)
	{
		union
		{
			float f;
			uint32_t i;
		} conv;

		const float x2 = number * 0.5f;
		conv.f = number;
		conv.i = 0x5f3759df - (conv.i >> 1);
		conv.f = conv.f * (1.5f - (x2 * conv.f * conv.f));
		return conv.f;
	}

	void SetupTreeParams(RE::BSFadeNode* fadeNode, DirectX::XMVECTOR& treeParams)
	{
		float invDistToCamera = 0.0f;
		float leafAmplitude = 1.0f;
		float leafFrequency = 1.0f;
		float windTimer = 0.f;

		if (auto* leafAnimNode = fadeNode->AsLeafAnimNode()) {
			//invDistToCamera = 1.0f / std::sqrt(leafAnimNode->unk158);
			invDistToCamera = FastInvSqrt(leafAnimNode->unk158);
			leafAmplitude = leafAnimNode->leafAmplitude;
			leafFrequency = leafAnimNode->leafFrequency;
			windTimer = leafAnimNode->unk164 * 6.f;
		}

		static auto& shaderManager = RE::BSShaderManager::State::GetSingleton();
		const float dampenStart = shaderManager.leafAnimDampenDistStartSPU;
		const float dampenEnd = shaderManager.leafAnimDampenDistEndSPU;
		const float clampedAmplitude = std::min(leafAmplitude, std::max((1.0f - ((invDistToCamera - dampenStart) / (dampenEnd - dampenStart))) * leafAmplitude, 0.0f));

		treeParams.m128_f32[0] = windTimer;
		treeParams.m128_f32[1] = shaderManager.shadowSceneNode[0]->GetRuntimeData().unk304;
		treeParams.m128_f32[2] = clampedAmplitude;
		treeParams.m128_f32[3] = leafFrequency;
	}

	void SetupInstancesUtility(RE::BSRenderPass* const * passes, uint32_t instanceCount, stl::enumeration<SIE::ShaderCache::UtilityShaderFlags> flags)
	{
		static auto* csState = State::GetSingleton();
		static auto& shaderManager = RE::BSShaderManager::State::GetSingleton();

		auto* buffer = csState->utilityInstanceBuffer->resource.get();

		D3D11_MAPPED_SUBRESOURCE mappedBuffer;
		csState->context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBuffer);

		const bool isTree = flags.any(SIE::ShaderCache::UtilityShaderFlags::TreeAnim);

		for (uint32_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
			auto& perInstance = static_cast<State::UtilityPerInstanceData*>(mappedBuffer.pData)[instanceIndex];
			perInstance.world = DirectX::XMMatrixTranspose(GetXMFromNi(passes[instanceIndex]->geometry->world));

			if (isTree)
			{
				SetupTreeParams(passes[instanceIndex]->shaderProperty->fadeNode, perInstance.treeParams);
			}
		}

		csState->context->Unmap(buffer, 0);
	}

	void SetupInstancesLighting(RE::BSRenderPass* const* passes, uint32_t instanceCount, SIE::ShaderCache::LightingShaderTechniques technique, [[maybe_unused]] stl::enumeration<SIE::ShaderCache::LightingShaderFlags> flags)
	{
		static auto* csState = State::GetSingleton();
		static auto& shaderManager = RE::BSShaderManager::State::GetSingleton();

		auto* buffer = csState->lightingInstanceBuffer->resource.get();

		D3D11_MAPPED_SUBRESOURCE mappedBuffer;
		csState->context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBuffer);

		const bool isTree = technique == SIE::ShaderCache::LightingShaderTechniques::TreeAnim;
		const RE::NiPoint3 previousPosAdjust = csState->shadowState->GetRuntimeData().previousPosAdjust.getEye(0);

		for (uint32_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
			auto& perInstance = static_cast<State::LightingPerInstanceData*>(mappedBuffer.pData)[instanceIndex];

			GeometrySetupConstantWorld(perInstance.world.f, passes[instanceIndex]->geometry->world, nullptr);
			GeometrySetupConstantWorld(perInstance.previousWorld.f, passes[instanceIndex]->geometry->previousWorld, &previousPosAdjust);

			if (isTree) {
				SetupTreeParams(passes[instanceIndex]->shaderProperty->fadeNode, perInstance.treeParams);
			}
		}

		csState->context->Unmap(buffer, 0);
	}

	void hk_Renderer_DrawTriShape(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::TriShape* shape, uint32_t startIndex, uint32_t count);
	decltype(&hk_Renderer_DrawTriShape) ptr_Renderer_DrawTriShape;
	void hk_Renderer_DrawTriShape([[maybe_unused]] RE::BSGraphics::Renderer* renderer, RE::BSGraphics::TriShape* shape, uint32_t startIndex, uint32_t count)
	{
		static auto* csState = State::GetSingleton();
		static auto* shadowState = csState->shadowState;
		static auto* context = csState->context;

		shadowState->SetVertexDesc(shape->vertexDesc);
		shadowState->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		hk_BSGraphics_SetDirtyStates(false);
		context->IASetIndexBuffer(reinterpret_cast<ID3D11Buffer*>(shape->indexBuffer), DXGI_FORMAT_R16_UINT, 0);

		if (csState->isInstancedPass) {
			winrt::com_ptr<ID3D11InputLayout> instancedLayout;

			auto& instancedInputLayouts = csState->instancedInputLayouts[csState->instancedShaderType];
			const uint32_t currentTechnique = RE::BSBatchRenderer::GetCurrentTechnique();
			if (auto it = instancedInputLayouts.find(currentTechnique); it != instancedInputLayouts.end())
			{
				instancedLayout = it->second;
			}
			else
			{
				instancedLayout = CreateInputLayout(shape->vertexDesc);
				instancedInputLayouts.insert_or_assign(currentTechnique, instancedLayout);
			}

			context->IASetInputLayout(instancedLayout.get());

			ID3D11Buffer* vertexBuffers[2]{ reinterpret_cast<ID3D11Buffer*>(shape->vertexBuffer), csState->instancedShaderType == RE::BSShader::Type::Utility ? csState->utilityInstanceBuffer->resource.get() : csState->lightingInstanceBuffer->resource.get() };
			const UINT strides[2]{ shape->vertexDesc.GetStride(), csState->instancedShaderType == RE::BSShader::Type::Utility ? sizeof(State::UtilityPerInstanceData) : sizeof(State::LightingPerInstanceData) };
			const UINT offsets[2]{ 0, 0 };
			context->IASetVertexBuffers(0, 2, vertexBuffers, strides, offsets);

			context->DrawIndexedInstanced(3 * count, csState->instanceCount, startIndex, 0, 0);
		} else {
			const uint32_t stride = shape->vertexDesc.GetStride();
			const uint32_t offset = 0;
			context->IASetVertexBuffers(0, 1, reinterpret_cast<ID3D11Buffer**>(&shape->vertexBuffer), &stride, &offset);
			context->DrawIndexed(3 * count, startIndex, 0);
		}
	}

	bool CompareRenderPasses(const RE::BSRenderPass* first, const RE::BSRenderPass* second)
	{
		const auto materialOrder = first->shaderProperty->material <=> second->shaderProperty->material;
		if (materialOrder == std::strong_ordering::less) {
			return true;
		}
		if (materialOrder == std::strong_ordering::equal) {
			return first->geometry->GetGeometryRuntimeData().rendererData < second->geometry->GetGeometryRuntimeData().rendererData;
		}
		return false;
	}

	template <typename Tval>
	struct PointerHash
	{
		size_t operator()(const Tval* val) const
		{
			static const size_t shift = static_cast<size_t>(std::log2(1 + sizeof(Tval)));
			return reinterpret_cast<size_t>(val) >> shift;
		}
	};

	bool hk_BSBatchRenderer_RenderBatches(RE::BSBatchRenderer* batchRenderer, uint32_t& technique, uint32_t& bucketIndex, RE::BSSimpleList<uint32_t>** passIndexList, uint32_t renderFlags);
	decltype(&hk_BSBatchRenderer_RenderBatches) ptr_BSBatchRenderer_RenderBatches;
	bool hk_BSBatchRenderer_RenderBatches(RE::BSBatchRenderer* batchRenderer, uint32_t& technique, uint32_t& bucketIndex, RE::BSSimpleList<uint32_t>** passIndexList, uint32_t renderFlags)
	{
		auto* csState = State::GetSingleton();
		auto* shadowState = csState->shadowState;

		if (csState->extendedFrameAnnotations) {
			State::GetSingleton()->BeginPerfEvent(std::format("BSBatchRenderer::RenderBatches ({:X})[{}] <{}>", technique, bucketIndex,
				renderFlags));
		}

		uint32_t passGroupIndex = 0;
		if (auto it = batchRenderer->techniqueToPassGroupIndex.find(technique); it != batchRenderer->techniqueToPassGroupIndex.end()) {
			passGroupIndex = it->second;
		}

		auto& passGroup = batchRenderer->passGroups[passGroupIndex];

		bool canInstance = csState->automaticInstancing;
		uint32_t instancedTechnique = technique;
		RE::BSShader::Type instancedShaderType = RE::BSShader::Type::None;
		auto utilityFlags = stl::enumeration(static_cast<SIE::ShaderCache::UtilityShaderFlags>(technique - 0x2B));
		const auto lightingTechnique = static_cast<SIE::ShaderCache::LightingShaderTechniques>(0x3F & ((technique - 0x4800002D) >> 24));
		auto lightingFlags = stl::enumeration(static_cast<SIE::ShaderCache::LightingShaderFlags>(technique - 0x4800002D));

		struct SubBatch
		{
			std::array<RE::BSRenderPass*, 256> passes;
			uint32_t passCount = 0;
		};
		static eastl::fixed_hash_map<RE::BSGraphics::TriShape*, SubBatch, 1024, 1025, false, PointerHash<RE::BSGraphics::TriShape>> subBatches;

		static std::array<RE::BSRenderPass*, 8192> nonInstanceablePasses;
		uint32_t nonInstanceablePassCount = 0;

		subBatches.clear();

		if (canInstance) {
			if (auto* firstPass = passGroup.passes[bucketIndex]) {
				if (firstPass->shader->shaderType == RE::BSShader::Type::Utility) {
					using enum SIE::ShaderCache::UtilityShaderFlags;
					if (utilityFlags.any(Skinned)) {
						canInstance = false;
					}
					instancedShaderType = RE::BSShader::Type::Utility;
					utilityFlags.set(Instanced);
					instancedTechnique = 0x2B + static_cast<uint32_t>(utilityFlags.underlying());
				} else if (firstPass->shader->shaderType == RE::BSShader::Type::Lighting) {
					using enum SIE::ShaderCache::LightingShaderFlags;
					if (lightingFlags.any(Skinned)) {
						canInstance = false;
					}
					if (lightingTechnique != SIE::ShaderCache::LightingShaderTechniques::None && lightingTechnique != SIE::ShaderCache::LightingShaderTechniques::TreeAnim && lightingTechnique != SIE::ShaderCache::LightingShaderTechniques::Glowmap && lightingTechnique != SIE::ShaderCache::LightingShaderTechniques::Envmap) {
						canInstance = false;
					}
					instancedShaderType = RE::BSShader::Type::Lighting;
					lightingFlags.set(Instanced);
					instancedTechnique = 0x4800002D + static_cast<uint32_t>(lightingFlags.underlying());
				}
				else
				{
					canInstance = false;
				}

				if (canInstance) {
					for (auto* pass = firstPass; pass; pass = pass->passGroupNext) {
						auto& geometryData = pass->geometry->GetGeometryRuntimeData();
						const bool instanceable = geometryData.skinInstance == nullptr && pass->geometry->GetType() == RE::BSGeometry::Type::kTriShape && (pass->geometry->flags02 & 0x8) == 0;
						if (!instanceable) {
							nonInstanceablePasses[nonInstanceablePassCount++] = pass;
						} else {
							RE::BSGraphics::TriShape* rendererData = geometryData.rendererData;
							if (auto it = subBatches.find(rendererData); it != subBatches.end()) {
								it->second.passes[it->second.passCount++] = pass;
							} else {
								subBatches.insert_or_assign(rendererData, SubBatch{ { pass }, 1 });
							}
						}
					}
				}
			}
		}

		bool alphaTest = false;
		{
			static const REL::Relocation<bool*> enableAlphaToCoverage{ RELOCATION_ID(527661, 414575) };

			const bool forceCullMode = (renderFlags & 0x108) == 0;

			using enum RE::BSBatchRenderer::PassGroup::Bucket;
			switch (static_cast<RE::BSBatchRenderer::PassGroup::Bucket>(bucketIndex)) {
			case Default:
				if (forceCullMode) {
					shadowState->SetCullMode(RE::BSGraphics::RASTER_STATE_CULL_MODE_BACK);
				}
				shadowState->SetAlphaTestEnabled(false);
				shadowState->SetAlphaBlendAlphaToCoverage(false);
				break;

			case AlphaTest:
				if (forceCullMode) {
					shadowState->SetCullMode(RE::BSGraphics::RASTER_STATE_CULL_MODE_BACK);
				}
				shadowState->SetAlphaTestEnabled(true);
				if (*enableAlphaToCoverage) {
					shadowState->SetAlphaBlendAlphaToCoverage(true);
				}
				alphaTest = true;
				break;

			case TwoSided:
				if (forceCullMode) {
					shadowState->SetCullMode(RE::BSGraphics::RASTER_STATE_CULL_MODE_NO_CULLING);
				}
				shadowState->SetAlphaTestEnabled(false);
				shadowState->SetAlphaBlendAlphaToCoverage(false);
				break;

			case TwoSidedAlphaTest:
				if (forceCullMode) {
					shadowState->SetCullMode(RE::BSGraphics::RASTER_STATE_CULL_MODE_NO_CULLING);
				}
				shadowState->SetAlphaTestEnabled(true);
				if (*enableAlphaToCoverage) {
					shadowState->SetAlphaBlendAlphaToCoverage(true);
				}
				alphaTest = true;
				break;

			case AlphaTestNoAlphaToCoverage:
				if (forceCullMode) {
					shadowState->SetCullMode(RE::BSGraphics::RASTER_STATE_CULL_MODE_BACK);
				}
				shadowState->SetAlphaTestEnabled(true);
				shadowState->SetAlphaBlendAlphaToCoverage(false);
				alphaTest = true;
				break;
			}
		}

		if (!subBatches.empty() || nonInstanceablePassCount > 0) {
			bool hasInstanced = false;
			for (uint32_t passIndex = 0; passIndex < nonInstanceablePassCount; ++passIndex) {
				RE::BSBatchRenderer::RenderPassImmediately(nonInstanceablePasses[passIndex], technique, alphaTest, renderFlags);
			}
			for (const auto& [rendererData, data] : subBatches)
			{
				if (data.passCount < static_cast<uint32_t>(csState->minInstanceCount)) {
					for (uint32_t passIndex = 0; passIndex < data.passCount; ++passIndex) {
						RE::BSBatchRenderer::RenderPassImmediately(data.passes[passIndex], technique, alphaTest, renderFlags);
					}
				}
				else
				{
					hasInstanced = true;
				}
			}
			if (hasInstanced) {
				csState->isInstancedPass = true;
				csState->instancedShaderType = instancedShaderType;

				RE::BSShaderManager::State::GetSingleton().currentShaderTechnique = technique;
				batchRenderer->EndPass();
				auto* defaultShader = subBatches.begin()->second.passes[0]->shader;
				if (defaultShader->SetupTechnique(instancedTechnique)) {
					RE::BSBatchRenderer::GetCurrentShader() = defaultShader;
					RE::BSBatchRenderer::GetCurrentTechnique() = instancedTechnique;

					for (const auto& [rendererData, data] : subBatches) {
						if (data.passCount >= static_cast<uint32_t>(csState->minInstanceCount)) {
							auto* firstPass = data.passes[0];
							csState->instanceCount = data.passCount;

							auto* material = firstPass->shaderProperty->material;
							if (material != RE::BSBatchRenderer::GetCurrentMaterial()) {
								if (material != nullptr) {
									firstPass->shader->SetupMaterial(material);
								}
								RE::BSBatchRenderer::GetCurrentMaterial() = material;
							}

							RE::BSBatchRenderer::SetupGeometry(firstPass, firstPass->shader, alphaTest || RE::BSGraphics::State::GetSingleton()->useEarlyZ, renderFlags);
							if (instancedShaderType == RE::BSShader::Type::Lighting) {
								SetupInstancesLighting(data.passes.data(), data.passCount, lightingTechnique, lightingFlags);
							} else {
								SetupInstancesUtility(data.passes.data(), data.passCount, utilityFlags);
							}
							
							RE::BSBatchRenderer::Draw(firstPass);
							firstPass->shader->RestoreGeometry(firstPass, renderFlags);

							csState->instanceCount = 0;
						}
					}
				} else {
					RE::BSBatchRenderer::GetCurrentShader() = nullptr;
					RE::BSBatchRenderer::GetCurrentTechnique() = 0;
				}

				csState->isInstancedPass = false;
				csState->instancedShaderType = RE::BSShader::Type::None;
			}
		} else {
			for (auto currentPass = passGroup.passes[bucketIndex]; currentPass; currentPass = currentPass->passGroupNext) {
				RE::BSBatchRenderer::RenderPassImmediately(currentPass, technique, alphaTest, renderFlags);
			}
		}

		if (batchRenderer->autoClearPasses) {
			passGroup.validBuckets &= ~(1 << bucketIndex);
			passGroup.passes[bucketIndex] = nullptr;
		}

		batchRenderer->EndPass();

		shadowState->SetAlphaBlendAlphaToCoverage(false);

		++bucketIndex;
		const auto result = batchRenderer->GetFirstOrNextNonEmptyTechniqueAndBucket(technique, bucketIndex, passIndexList);

		if (csState->extendedFrameAnnotations) {
			State::GetSingleton()->EndPerfEvent();
		}

		return result;
	}

	void Install()
	{
		SKSE::AllocTrampoline(14);
		auto& trampoline = SKSE::GetTrampoline();
		logger::info("Hooking BSInputDeviceManager::PollInputDevices");
		_InputFunc = trampoline.write_call<5>(REL::RelocationID(67315, 68617).address() + REL::Relocate(0x7B, 0x7B, 0x81), hk_PollInputDevices);  //BSInputDeviceManager::PollInputDevices -> Inputfunc

		logger::info("Hooking BSShader::LoadShaders");
		*(uintptr_t*)&ptr_BSShader_LoadShaders = Detours::X64::DetourFunction(REL::RelocationID(101339, 108326).address(), (uintptr_t)&hk_BSShader_LoadShaders);
		logger::info("Hooking BSShader::BeginTechnique");
		*(uintptr_t*)&ptr_BSShader_BeginTechnique = Detours::X64::DetourFunction(REL::RelocationID(101341, 108328).address(), (uintptr_t)&hk_BSShader_BeginTechnique);
		logger::info("Hooking BSGraphics::SetDirtyStates");
		*(uintptr_t*)&ptr_BSGraphics_SetDirtyStates = Detours::X64::DetourFunction(REL::RelocationID(75580, 77386).address(), (uintptr_t)&hk_BSGraphics_SetDirtyStates);

		logger::info("Hooking BSGraphics::Renderer::InitD3D");
		stl::write_thunk_call<BSGraphics_Renderer_Init_InitD3D>(REL::RelocationID(75595, 77226).address() + REL::Relocate(0x50, 0x2BC));

		logger::info("Hooking deferred passes");
		stl::write_vfunc<0x2, BSImagespaceShaderISSAOCompositeSAO_SetupTechnique>(RE::VTABLE_BSImagespaceShaderISSAOCompositeSAO[0]);
		stl::write_vfunc<0x2, BSImagespaceShaderISSAOCompositeFog_SetupTechnique>(RE::VTABLE_BSImagespaceShaderISSAOCompositeFog[0]);
		stl::write_vfunc<0x2, BSImagespaceShaderISSAOCompositeSAOFog_SetupTechnique>(RE::VTABLE_BSImagespaceShaderISSAOCompositeSAOFog[0]);

		logger::info("Hooking preprocess passes");
		stl::write_vfunc<0x2, BSImagespaceShaderHDRTonemapBlendCinematic_SetupTechnique>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematic[0]);
		stl::write_vfunc<0x2, BSImagespaceShaderHDRTonemapBlendCinematicFade_SetupTechnique>(RE::VTABLE_BSImagespaceShaderHDRTonemapBlendCinematicFade[0]);

		logger::info("Hooking WndProcHandler");
		stl::write_thunk_call_6<RegisterClassA_Hook>(REL::VariantID(75591, 77226, 0xDC4B90).address() + REL::VariantOffset(0x8E, 0x15C, 0x99).offset());

		//logger::info("Hooking D3D11CreateDeviceAndSwapChain");
		//*(FARPROC*)&ptrD3D11CreateDeviceAndSwapChain = GetProcAddress(GetModuleHandleA("d3d11.dll"), "D3D11CreateDeviceAndSwapChain");
		//SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");

		logger::info("Hooking BSShaderRenderTargets::Create");
		*(uintptr_t*)&ptr_BSShaderRenderTargets_Create = Detours::X64::DetourFunction(REL::RelocationID(100458, 107175).address(), (uintptr_t)&hk_BSShaderRenderTargets_Create);

		logger::info("Hooking BSShaderRenderTargets::Create::CreateRenderTarget(s)");
		stl::write_thunk_call<CreateRenderTarget_Main>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x3F0, 0x3F3, 0x548));
		stl::write_thunk_call<CreateRenderTarget_Normals>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x458, 0x45B, 0x5B0));
		stl::write_thunk_call<CreateRenderTarget_NormalsSwap>(REL::RelocationID(100458, 107175).address() + REL::Relocate(0x46B, 0x46E, 0x5C3));

		logger::info("Hooking BSLightingShaderProperty");
		stl::write_vfunc<0x18, BSLightingShaderProperty_LoadBinary>(RE::VTABLE_BSLightingShaderProperty[0]);
		stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);

		logger::info("Hooking BSLightingShader");
		stl::write_vfunc<0x4, BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
		stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
		std::ignore = Detours::X64::DetourFunction(REL::RelocationID(101633, 108700).address(), (uintptr_t)&hk_BSLightingShader_GetPixelTechnique);

		logger::info("Hooking BSLightingShaderMaterialBase");
		std::ignore = Detours::X64::DetourFunction(REL::RelocationID(100016, 106723).address(), (uintptr_t)&hk_BSLightingShaderMaterialBase_CreateMaterial);

		logger::info("Hooking TESObjectLAND");
		*(uintptr_t*)&ptr_TESObjectLAND_SetupMaterial = Detours::X64::DetourFunction(REL::RelocationID(18368, 18791).address(), (uintptr_t)&hk_TESObjectLAND_SetupMaterial);

		logger::info("Hooking TESLandTexture");
		stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
		stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
		stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
		stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);

		logger::info("Hooking SetPerFrameBuffers");
		*(uintptr_t*)&ptr_SetPerFrameBuffers = Detours::X64::DetourFunction(REL::RelocationID(75570, 77371).address(), (uintptr_t)&hk_SetPerFrameBuffers);

		logger::info("Hooking BSTempEffectSimpleDecal");
		*(uintptr_t*)&ptr_BSTempEffectSimpleDecal_SetupGeometry = Detours::X64::DetourFunction(REL::RelocationID(29253, 30108).address(), (uintptr_t)&hk_BSTempEffectSimpleDecal_SetupGeometry);

		logger::info("Hooking BSTempEffectGeometryDecal");
		stl::write_vfunc<0x25, BSTempEffectGeometryDecal_Initialize>(RE::VTABLE_BSTempEffectGeometryDecal[0]);

		logger::info("Hooking BSGrassShaderProperty::ctor");
		stl::write_thunk_call<BSGrassShaderProperty_ctor>(REL::RelocationID(15214, 15383).address() + REL::Relocate(0x45B, 0x4F5));

		logger::info("Hooking BSGrassShaderProperty");
		stl::write_vfunc<0x2A, BSGrassShaderProperty_GetRenderPasses>(RE::VTABLE_BSGrassShaderProperty[0]);

		logger::info("Hooking BSGrassShader");
		stl::write_vfunc<0x2, BSGrassShader_SetupTechnique>(RE::VTABLE_BSGrassShader[0]);
		stl::write_vfunc<0x4, BSGrassShader_SetupMaterial>(RE::VTABLE_BSGrassShader[0]);

		*(uintptr_t*)&ptr_Renderer_DrawTriShape = Detours::X64::DetourFunction(REL::RelocationID(75477, 77263).address(), (uintptr_t)&hk_Renderer_DrawTriShape);
		*(uintptr_t*)&ptr_BSBatchRenderer_RenderBatches = Detours::X64::DetourFunction(REL::RelocationID(100852, 107642).address(), (uintptr_t)&hk_BSBatchRenderer_RenderBatches);
	}
}