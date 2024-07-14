#include "State.h"

#include <magic_enum.hpp>
#include <pystring/pystring.h>

#include "Menu.h"
#include "ShaderCache.h"

#include "Feature.h"
#include "Util.h"

#include "Deferred.h"
#include "Features/TerrainBlending.h"

#include "VariableRateShading.h"

namespace PNState
{
	template<typename ResultType>
	bool Read(const json& config, ResultType& result)
	{
		if constexpr (std::is_same_v<ResultType, std::array<float, 3>> || std::is_same_v<ResultType, RE::NiColor>) {
			if (config.is_array() && config.size() == 3 &&
				config[0].is_number_float() && config[1].is_number_float() &&
				config[2].is_number_float()) {
				result[0] = config[0];
				result[1] = config[1];
				result[2] = config[2];
				return true;
			}
		}
		if constexpr (std::is_same_v<ResultType, float>) {
			if (config.is_number_float()) {
				result = config;
				return true;
			}
		}
		return false;
	}

	void ReadPBRRecordConfigs(const std::string& rootPath, std::function<void(const std::string&, const json&)> recordReader)
	{
		if (std::filesystem::exists(rootPath)) {
			auto configs = clib_util::distribution::get_configs(rootPath, "", ".json");

			if (configs.empty()) {
				logger::warn("[TruePBR] no .json files were found within the {} folder, aborting...", rootPath);
				return;
			}

			logger::info("[TruePBR] {} matching jsons found", configs.size());

			for (auto& path : configs) {
				logger::info("[TruePBR] loading json : {}", path);

				std::ifstream fileStream(path);
				if (!fileStream.is_open()) {
					logger::error("[TruePBR] failed to read {}", path);
					continue;
				}

				json config;
				try {
					fileStream >> config;
				} catch (const nlohmann::json::parse_error& e) {
					logger::error("[TruePBR] failed to parse {} : {}", path, e.what());
					continue;
				}

				const auto editorId = std::filesystem::path(path).stem().string();
				recordReader(editorId, config);
			}
		}
	}

	void SavePBRRecordConfig(const std::string& rootPath, const std::string& editorId, const json& config)
	{
		std::filesystem::create_directory(rootPath);

		const std::string outputPath = std::format("{}\\{}.json", rootPath, editorId);
		std::ofstream fileStream(outputPath);
		if (!fileStream.is_open()) {
			logger::error("[TruePBR] failed to write {}", outputPath);
			return;
		}
		try {
			fileStream << std::setw(4) << config;
		} catch (const nlohmann::json::type_error& e) {
			logger::error("[TruePBR] failed to serialize {} : {}", outputPath, e.what());
			return;
		}
	}
}

void State::Draw()
{
	auto terrainBlending = TerrainBlending::GetSingleton();
	if (terrainBlending->loaded)
		terrainBlending->TerrainShaderHacks();

	if (currentShader && updateShader) {
		auto type = currentShader->shaderType.get();
		if (type == RE::BSShader::Type::Utility) {
			if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
				Deferred::GetSingleton()->CopyShadowData();
			}
		}
		auto& shaderCache = SIE::ShaderCache::Instance();
		if (shaderCache.IsEnabled()) {
			VariableRateShading::GetSingleton()->UpdateViews(type != RE::BSShader::Type::ImageSpace && type != RE::BSShader::Type::Sky && type != RE::BSShader::Type::Water);
			if (type > 0 && type < RE::BSShader::Type::Total) {
				if (enabledClasses[type - 1]) {
					// Only check against non-shader bits
					currentPixelDescriptor &= ~modifiedPixelDescriptor;
					if (currentPixelDescriptor != lastPixelDescriptor) {
						PermutationCB data{};
						data.VertexShaderDescriptor = currentVertexDescriptor;
						data.PixelShaderDescriptor = currentPixelDescriptor;

						permutationCB->Update(data);

						lastVertexDescriptor = currentVertexDescriptor;
						lastPixelDescriptor = currentPixelDescriptor;

						ID3D11Buffer* buffers[3] = { permutationCB->CB(), sharedDataCB->CB(), featureDataCB->CB() };
						context->PSSetConstantBuffers(4, 3, buffers);
					}

					if (IsDeveloperMode()) {
						BeginPerfEvent(std::format("Draw: CS {}::{:x}", magic_enum::enum_name(currentShader->shaderType.get()), currentPixelDescriptor));
						SetPerfMarker(std::format("Defines: {}", SIE::ShaderCache::GetDefinesString(currentShader->shaderType.get(), currentPixelDescriptor)));
						EndPerfEvent();
					}
				}
			}
		}
	}
	updateShader = false;
}

void State::Reset()
{
	for (auto* feature : Feature::GetFeatureList())
		if (feature->loaded)
			feature->Reset();
	if (!RE::UI::GetSingleton()->GameIsPaused())
		timer += RE::GetSecondsSinceLastFrame();
	VariableRateShading::GetSingleton()->UpdateVRS();
	lastModifiedPixelDescriptor = 0;
	lastModifiedVertexDescriptor = 0;
	lastPixelDescriptor = 0;
	lastVertexDescriptor = 0;
	initialized = false;
}

void State::Setup()
{
	SetupTextureSetData();
	SetupMaterialObjectData();
	SetupLightingTemplateData();
	SetupWeatherData();
	SetupResources();
	for (auto* feature : Feature::GetFeatureList())
		if (feature->loaded)
			feature->SetupResources();
	Deferred::GetSingleton()->SetupResources();
	if (initialized)
		return;
	VariableRateShading::GetSingleton()->Setup();
	initialized = true;
}

static const std::string& GetConfigPath(State::ConfigMode a_configMode)
{
	switch (a_configMode) {
	case State::ConfigMode::USER:
		return State::GetSingleton()->userConfigPath;
	case State::ConfigMode::TEST:
		return State::GetSingleton()->testConfigPath;
	case State::ConfigMode::DEFAULT:
	default:
		return State::GetSingleton()->defaultConfigPath;
	}
}

void State::Load(ConfigMode a_configMode)
{
	ConfigMode configMode = a_configMode;
	auto& shaderCache = SIE::ShaderCache::Instance();

	std::string configPath = GetConfigPath(configMode);
	std::ifstream i(configPath);
	if (!i.is_open()) {
		logger::info("Unable to open user config file ({}); trying default ({})", configPath, defaultConfigPath);
		configMode = ConfigMode::DEFAULT;
		configPath = GetConfigPath(configMode);
		i.open(configPath);
		if (!i.is_open()) {
			logger::info("No default config ({}), generating new one", configPath);
			std::fill(enabledClasses, enabledClasses + RE::BSShader::Type::Total - 1, true);
			enabledClasses[RE::BSShader::Type::ImageSpace - 1] = false;
			enabledClasses[RE::BSShader::Type::Utility - 1] = false;
			Save(configMode);
			i.open(configPath);
			if (!i.is_open()) {
				logger::error("Error opening config file ({})\n", configPath);
				return;
			}
		}
	}
	logger::info("Loading config file ({})", configPath);

	json settings;
	try {
		i >> settings;
	} catch (const nlohmann::json::parse_error& e) {
		logger::error("Error parsing json config file ({}) : {}\n", configPath, e.what());
		return;
	}

	if (settings["Menu"].is_object()) {
		Menu::GetSingleton()->Load(settings["Menu"]);
	}

	if (settings["Advanced"].is_object()) {
		json& advanced = settings["Advanced"];
		if (advanced["Dump Shaders"].is_boolean())
			shaderCache.SetDump(advanced["Dump Shaders"]);
		if (advanced["Log Level"].is_number_integer()) {
			logLevel = static_cast<spdlog::level::level_enum>((int)advanced["Log Level"]);
		}
		if (advanced["Shader Defines"].is_string())
			SetDefines(advanced["Shader Defines"]);
		if (advanced["Compiler Threads"].is_number_integer())
			shaderCache.compilationThreadCount = std::clamp(advanced["Compiler Threads"].get<int32_t>(), 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (advanced["Background Compiler Threads"].is_number_integer())
			shaderCache.backgroundCompilationThreadCount = std::clamp(advanced["Background Compiler Threads"].get<int32_t>(), 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (advanced["Use FileWatcher"].is_boolean())
			shaderCache.SetFileWatcher(advanced["Use FileWatcher"]);
		if (advanced["Extended Frame Annotations"].is_boolean())
			extendedFrameAnnotations = advanced["Extended Frame Annotations"];
	}

	if (settings["General"].is_object()) {
		json& general = settings["General"];

		if (general["Enable Shaders"].is_boolean())
			shaderCache.SetEnabled(general["Enable Shaders"]);

		if (general["Enable Disk Cache"].is_boolean())
			shaderCache.SetDiskCache(general["Enable Disk Cache"]);

		if (general["Enable Async"].is_boolean())
			shaderCache.SetAsync(general["Enable Async"]);
	}

	if (settings["Replace Original Shaders"].is_object()) {
		json& originalShaders = settings["Replace Original Shaders"];
		for (int classIndex = 0; classIndex < RE::BSShader::Type::Total - 1; ++classIndex) {
			auto name = magic_enum::enum_name((RE::BSShader::Type)(classIndex + 1));
			if (originalShaders[name].is_boolean()) {
				enabledClasses[classIndex] = originalShaders[name];
			}
		}
	}

	if (settings["PBR"].is_object()) {
		json& pbr = settings["PBR"];

		if (pbr["Use Dynamic Cubemap"].is_boolean()) {
			pbrSettings.useDynamicCubemap = pbr["Use Dynamic Cubemap"];
		}
		if (pbr["Match Dynamic Cubemap Color To Ambient"].is_boolean()) {
			pbrSettings.matchDynamicCubemapColorToAmbient = pbr["Match Dynamic Cubemap Color To Ambient"];
		}
		if (pbr["Use Multiple Scattering"].is_boolean()) {
			pbrSettings.useMultipleScattering = pbr["Use Multiple Scattering"];
		}
		if (pbr["Use Multi-bounce AO"].is_boolean()) {
			pbrSettings.useMultiBounceAO = pbr["Use Multi-bounce AO"];
		}
		if (pbr["Diffuse Model"].is_number_integer()) {
			pbrSettings.diffuseModel = pbr["Diffuse Model"];
		}

		if (pbr["Light Color Multiplier"].is_number_float()) {
			globalPBRLightColorMultiplier = pbr["Light Color Multiplier"];
		}
		if (pbr["Light Color Power"].is_number_float()) {
			globalPBRLightColorPower = pbr["Light Color Power"];
		}
		if (pbr["Ambient Light Color Multiplier"].is_number_float()) {
			globalPBRAmbientLightColorMultiplier = pbr["Ambient Light Color Multiplier"];
		}
		if (pbr["Ambient Light Color Power"].is_number_float()) {
			globalPBRAmbientLightColorPower = pbr["Ambient Light Color Power"];
		}
	}

	for (auto* feature : Feature::GetFeatureList())
		feature->Load(settings);
	i.close();
	if (settings["Version"].is_string() && settings["Version"].get<std::string>() != Plugin::VERSION.string()) {
		logger::info("Found older config for version {}; upgrading to {}", (std::string)settings["Version"], Plugin::VERSION.string());
		Save(configMode);
	}
}

void State::Save(ConfigMode a_configMode)
{
	auto& shaderCache = SIE::ShaderCache::Instance();
	std::string configPath = GetConfigPath(a_configMode);
	std::ofstream o{ configPath };
	json settings;

	Menu::GetSingleton()->Save(settings);

	json advanced;
	advanced["Dump Shaders"] = shaderCache.IsDump();
	advanced["Log Level"] = logLevel;
	advanced["Shader Defines"] = shaderDefinesString;
	advanced["Compiler Threads"] = shaderCache.compilationThreadCount;
	advanced["Background Compiler Threads"] = shaderCache.backgroundCompilationThreadCount;
	advanced["Use FileWatcher"] = shaderCache.UseFileWatcher();
	advanced["Extended Frame Annotations"] = extendedFrameAnnotations;
	settings["Advanced"] = advanced;

	json general;
	general["Enable Shaders"] = shaderCache.IsEnabled();
	general["Enable Disk Cache"] = shaderCache.IsDiskCache();
	general["Enable Async"] = shaderCache.IsAsync();

	settings["General"] = general;

	{
		json pbr;
		pbr["Use Dynamic Cubemap"] = pbrSettings.useDynamicCubemap;
		pbr["Match Dynamic Cubemap Color To Ambient"] = pbrSettings.matchDynamicCubemapColorToAmbient;
		pbr["Use Multiple Scattering"] = pbrSettings.useMultipleScattering;
		pbr["Use Multi-bounce AO"] = pbrSettings.useMultiBounceAO;
		pbr["Diffuse Model"] = pbrSettings.diffuseModel;

		pbr["Light Color Multiplier"] = globalPBRLightColorMultiplier;
		pbr["Light Color Power"] = globalPBRLightColorPower;
		pbr["Ambient Light Color Multiplier"] = globalPBRAmbientLightColorMultiplier;
		pbr["Ambient Light Color Power"] = globalPBRAmbientLightColorPower;
		settings["PBR"] = pbr;
	}

	json originalShaders;
	for (int classIndex = 0; classIndex < RE::BSShader::Type::Total - 1; ++classIndex) {
		originalShaders[magic_enum::enum_name((RE::BSShader::Type)(classIndex + 1))] = enabledClasses[classIndex];
	}
	settings["Replace Original Shaders"] = originalShaders;

	settings["Version"] = Plugin::VERSION.string();

	for (auto* feature : Feature::GetFeatureList())
		feature->Save(settings);

	o << settings.dump(1);
	logger::info("Saving settings to {}", configPath);
}

void State::PostPostLoad()
{
	upscalerLoaded = GetModuleHandle(L"Data\\SKSE\\Plugins\\SkyrimUpscaler.dll");
	if (upscalerLoaded)
		logger::info("Skyrim Upscaler detected");
	else
		logger::info("Skyrim Upscaler not detected");
	Deferred::Hooks::Install();
}

bool State::ValidateCache(CSimpleIniA& a_ini)
{
	bool valid = true;
	for (auto* feature : Feature::GetFeatureList())
		valid = valid && feature->ValidateCache(a_ini);
	return valid;
}

void State::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	for (auto* feature : Feature::GetFeatureList())
		feature->WriteDiskCacheInfo(a_ini);
}

void State::SetLogLevel(spdlog::level::level_enum a_level)
{
	logLevel = a_level;
	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), static_cast<int>(logLevel));
}

spdlog::level::level_enum State::GetLogLevel()
{
	return logLevel;
}

void State::SetDefines(std::string a_defines)
{
	shaderDefines.clear();
	shaderDefinesString = "";
	std::string name = "";
	std::string definition = "";
	auto defines = pystring::split(a_defines, ";");
	for (const auto& define : defines) {
		auto cleanedDefine = pystring::strip(define);
		auto token = pystring::split(cleanedDefine, "=");
		if (token.empty() || token[0].empty())
			continue;
		if (token.size() > 2) {
			logger::warn("Define string has too many '='; ignoring {}", define);
			continue;
		}
		name = pystring::strip(token[0]);
		if (token.size() == 2) {
			definition = pystring::strip(token[1]);
		}
		shaderDefinesString += pystring::strip(define) + ";";
		shaderDefines.push_back(std::pair(name, definition));
	}
	shaderDefinesString = shaderDefinesString.substr(0, shaderDefinesString.size() - 1);
	logger::debug("Shader Defines set to {}", shaderDefinesString);
}

std::vector<std::pair<std::string, std::string>>* State::GetDefines()
{
	return &shaderDefines;
}

bool State::ShaderEnabled(const RE::BSShader::Type a_type)
{
	auto index = static_cast<uint32_t>(a_type) + 1;
	if (index && index < sizeof(enabledClasses)) {
		return enabledClasses[index];
	}
	return false;
}

bool State::IsShaderEnabled(const RE::BSShader& a_shader)
{
	return ShaderEnabled(a_shader.shaderType.get());
}

bool State::IsDeveloperMode()
{
	return GetLogLevel() <= spdlog::level::debug;
}

void State::ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
{
	a_properties->supportUnorderedAccess = true;
	logger::debug("Adding UAV access to {}", magic_enum::enum_name(a_target));
}

void State::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();

	permutationCB = new ConstantBuffer(ConstantBufferDesc<PermutationCB>());
	sharedDataCB = new ConstantBuffer(ConstantBufferDesc<SharedDataCB>());

	auto [data, size] = GetFeatureBufferData();
	featureDataCB = new ConstantBuffer(ConstantBufferDesc((uint32_t)size));
	delete[] data;

	// Grab main texture to get resolution
	// VR cannot use viewport->screenWidth/Height as it's the desktop preview window's resolution and not HMD
	D3D11_TEXTURE2D_DESC texDesc{};
	renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&texDesc);

	isVR = REL::Module::IsVR();
	screenWidth = (float)texDesc.Width;
	screenHeight = (float)texDesc.Height;
	context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
	device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);
	context->QueryInterface(__uuidof(pPerf), reinterpret_cast<void**>(&pPerf));
}

void State::ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred)
{
	if (a_shader.shaderType.get() != RE::BSShader::Type::Utility && a_shader.shaderType.get() != RE::BSShader::Type::ImageSpace) {
		switch (a_shader.shaderType.get()) {
		case RE::BSShader::Type::Lighting:
			{
				a_vertexDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::RimLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::SoftLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BackLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Specular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AnisoLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight);

				static auto enableImprovedSnow = RE::GetINISetting("bEnableImprovedSnow:Display");
				static bool vr = REL::Module::IsVR();

				if (vr || !enableImprovedSnow->GetBool())
					a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow);

				if (Deferred::GetSingleton()->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::Deferred;

				{
					uint32_t technique = 0x3F & (a_vertexDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Parallax ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Facegen ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::FacegenRGBTint ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjects ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjectHD ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::MultiIndexSparkle ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Hair)
						a_vertexDescriptor &= ~(0x3F << 24);
				}

				{
					uint32_t technique = 0x3F & (a_pixelDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap)
						a_pixelDescriptor &= ~(0x3F << 24);
				}
			}
			break;
		case RE::BSShader::Type::Water:
			{
				a_vertexDescriptor &= ~((uint32_t)SIE::ShaderCache::WaterShaderFlags::Reflections |
										(uint32_t)SIE::ShaderCache::WaterShaderFlags::Cubemap |
										(uint32_t)SIE::ShaderCache::WaterShaderFlags::Interior |
										(uint32_t)SIE::ShaderCache::WaterShaderFlags::Reflections);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::WaterShaderFlags::Reflections |
									   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Cubemap |
									   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Interior);
			}
			break;
		case RE::BSShader::Type::Effect:
			{
				a_vertexDescriptor &= ~((uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor |
										(uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToAlpha |
										(uint32_t)SIE::ShaderCache::EffectShaderFlags::IgnoreTexAlpha);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor |
									   (uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToAlpha |
									   (uint32_t)SIE::ShaderCache::EffectShaderFlags::IgnoreTexAlpha);

				if (Deferred::GetSingleton()->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::EffectShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::DistantTree:
			{
				if (Deferred::GetSingleton()->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::DistantTreeShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::Sky:
			{
				if (Deferred::GetSingleton()->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= 256;
			}
			break;
		}
	}
}

void State::BeginPerfEvent(std::string_view title)
{
	pPerf->BeginEvent(std::wstring(title.begin(), title.end()).c_str());
}

void State::EndPerfEvent()
{
	pPerf->EndEvent();
}

void State::SetPerfMarker(std::string_view title)
{
	pPerf->SetMarker(std::wstring(title.begin(), title.end()).c_str());
}

void State::UpdateSharedData()
{
	{
		SharedDataCB data{};

		auto& shaderManager = RE::BSShaderManager::State::GetSingleton();
		RE::NiTransform& dalcTransform = shaderManager.directionalAmbientTransform;
		Util::StoreTransform3x4NoScale(data.DirectionalAmbient, dalcTransform);

		auto shadowSceneNode = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		data.DirLightColor = { dirLight->GetLightRuntimeData().diffuse.red, dirLight->GetLightRuntimeData().diffuse.green, dirLight->GetLightRuntimeData().diffuse.blue, 1.0f };

		auto imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		data.DirLightColor *= !isVR ? imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale : imageSpaceManager->GetVRRuntimeData().data.baseData.hdr.sunlightScale;

		auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { screenWidth, screenHeight };
		data.Timer = timer;

		auto viewport = RE::BSGraphics::State::GetSingleton();

		auto bTAA = !REL::Module::IsVR() ? imageSpaceManager->GetRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled :
		                                   imageSpaceManager->GetVRRuntimeData().BSImagespaceShaderISTemporalAA->taaEnabled;

		data.FrameCount = viewport->uiFrameCount * (bTAA || State::GetSingleton()->upscalerLoaded);

		for (int i = -2; i <= 2; i++) {
			for (int k = -2; k <= 2; k++) {
				int waterTile = (i + 2) + ((k + 2) * 5);
				data.WaterData[waterTile] = Util::TryGetWaterData((float)i * 4096.0f, (float)k * 4096.0f);
			}
		}

		sharedDataCB->Update(data);
	}

	{
		auto [data, size] = GetFeatureBufferData();

		featureDataCB->Update(data, size);

		delete[] data;
	}

	auto depth = RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
	context->PSSetShaderResources(20, 1, &depth);
}

void State::SetupFrame()
{
	float newDirectionalLightScale = 1.f;
	float newDirectionalAmbientLightScale = 1.f;

	if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
		if (const auto* currentCell = player->GetParentCell()) {
			if (currentCell->IsInteriorCell()) {
				if (const auto* lightingTemplate = currentCell->GetRuntimeData().lightingTemplate) {
					const auto* editorId = lightingTemplate->GetFormEditorID();
					if (auto it = pbrLightingTemplates.find(editorId); it != pbrLightingTemplates.cend()) {
						newDirectionalLightScale = it->second.directionalLightColorScale;
						newDirectionalAmbientLightScale = it->second.directionalAmbientLightColorScale;
					}
				}
			}
			else if (RE::Sky* sky = RE::Sky::GetSingleton()) {
				if (const auto* weather = sky->currentWeather) {
					const auto* editorId = weather->GetFormEditorID();
					if (auto it = pbrWeathers.find(editorId); it != pbrWeathers.cend()) {
						newDirectionalLightScale = it->second.directionalLightColorScale;
						newDirectionalAmbientLightScale = it->second.directionalAmbientLightColorScale;
					}
				}
			}
		}
	}

	weatherPBRDirectionalLightColorMultiplier = newDirectionalLightScale;
	weatherPBRDirectionalAmbientLightColorMultiplier = newDirectionalAmbientLightScale;

	pbrSettings.directionalLightColorMultiplier = globalPBRLightColorMultiplier * weatherPBRDirectionalLightColorMultiplier;
	pbrSettings.directionalLightColorPower = globalPBRLightColorPower;
	pbrSettings.pointLightColorMultiplier = globalPBRLightColorMultiplier;
	pbrSettings.pointLightColorPower = globalPBRLightColorPower;
	pbrSettings.ambientLightColorMultiplier = globalPBRAmbientLightColorMultiplier * weatherPBRDirectionalAmbientLightColorMultiplier;
	pbrSettings.ambientLightColorPower = globalPBRAmbientLightColorPower;
}

void State::SetupTextureSetData()
{
	logger::info("[TruePBR] loading PBR texture set configs");

	pbrTextureSets.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		PBRTextureSetData textureSetData;

		PNState::Read(config["roughnessScale"], textureSetData.roughnessScale);
		PNState::Read(config["displacementScale"], textureSetData.displacementScale);
		PNState::Read(config["specularLevel"], textureSetData.specularLevel);
		PNState::Read(config["subsurfaceColor"], textureSetData.subsurfaceColor);
		PNState::Read(config["subsurfaceOpacity"], textureSetData.subsurfaceOpacity);
		PNState::Read(config["coatColor"], textureSetData.coatColor);
		PNState::Read(config["coatStrength"], textureSetData.coatStrength);
		PNState::Read(config["coatRoughness"], textureSetData.coatRoughness);
		PNState::Read(config["coatSpecularLevel"], textureSetData.coatSpecularLevel);
		PNState::Read(config["innerLayerDisplacementOffset"], textureSetData.innerLayerDisplacementOffset);
		PNState::Read(config["fuzzColor"], textureSetData.fuzzColor);
		PNState::Read(config["fuzzWeight"], textureSetData.fuzzWeight);

		pbrTextureSets.insert_or_assign(editorId, textureSetData);
		});
}

State::PBRTextureSetData* State::GetPBRTextureSetData(const RE::TESForm* textureSet)
{
	if (textureSet == nullptr) {
		return nullptr;
	}

	auto it = pbrTextureSets.find(textureSet->GetFormEditorID());
	if (it == pbrTextureSets.end()) {
		return nullptr;
	}
	return &it->second;
}

bool State::IsPBRTextureSet(const RE::TESForm* textureSet)
{
	return GetPBRTextureSetData(textureSet) != nullptr;
}

void State::SetupMaterialObjectData()
{
	logger::info("[TruePBR] loading PBR material object configs");

	pbrMaterialObjects.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRMaterialObjects", [this](const std::string& editorId, const json& config) {
		PBRMaterialObjectData materialObjectData;

		PNState::Read(config["baseColorScale"], materialObjectData.baseColorScale);
		PNState::Read(config["roughness"], materialObjectData.roughness);
		PNState::Read(config["specularLevel"], materialObjectData.specularLevel);

		pbrMaterialObjects.insert_or_assign(editorId, materialObjectData);
		});
}

State::PBRMaterialObjectData* State::GetPBRMaterialObjectData(const RE::TESForm* materialObject)
{
	if (materialObject == nullptr) {
		return nullptr;
	}

	auto it = pbrMaterialObjects.find(materialObject->GetFormEditorID());
	if (it == pbrMaterialObjects.end()) {
		return nullptr;
	}
	return &it->second;
}

bool State::IsPBRMaterialObject(const RE::TESForm* materialObject)
{
	return GetPBRMaterialObjectData(materialObject) != nullptr;
}

void State::SetupLightingTemplateData()
{
	logger::info("[TruePBR] loading PBR lighting template configs");

	pbrLightingTemplates.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRLightingTemplates", [this](const std::string& editorId, const json& config) {
		PBRLightingTemplateData lightingTemplateData;

		PNState::Read(config["directionalLightColorScale"], lightingTemplateData.directionalLightColorScale);
		PNState::Read(config["directionalAmbientLightColorScale"], lightingTemplateData.directionalAmbientLightColorScale);

		pbrLightingTemplates.insert_or_assign(editorId, lightingTemplateData);
		});
}

State::PBRLightingTemplateData* State::GetPBRLightingTemplateData(const RE::TESForm* lightingTemplate)
{
	if (lightingTemplate == nullptr) {
		return nullptr;
	}

	auto it = pbrLightingTemplates.find(lightingTemplate->GetFormEditorID());
	if (it == pbrLightingTemplates.end()) {
		return nullptr;
	}
	return &it->second;
}

bool State::IsPBRLightingTemplate(const RE::TESForm* lightingTemplate)
{
	return GetPBRLightingTemplateData(lightingTemplate) != nullptr;
}

void State::SavePBRLightingTemplateData(const std::string& editorId)
{
	const auto& pbrLightingTemplateData = pbrLightingTemplates[editorId];

	json config;
	config["directionalLightColorScale"] = pbrLightingTemplateData.directionalLightColorScale;
	config["directionalAmbientLightColorScale"] = pbrLightingTemplateData.directionalAmbientLightColorScale;

	PNState::SavePBRRecordConfig("Data\\PBRLightingTemplates\\", editorId, config);
}

void State::SetupWeatherData()
{
	logger::info("[TruePBR] loading PBR weather configs");

	pbrWeathers.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRWeathers", [this](const std::string& editorId, const json& config) {
		PBRWeatherData weatherData;

		PNState::Read(config["directionalLightColorScale"], weatherData.directionalLightColorScale);
		PNState::Read(config["directionalAmbientLightColorScale"], weatherData.directionalAmbientLightColorScale);

		pbrWeathers.insert_or_assign(editorId, weatherData);
		});
}

State::PBRWeatherData* State::GetPBRWeatherData(const RE::TESForm* weather)
{
	if (weather == nullptr) {
		return nullptr;
	}

	auto it = pbrWeathers.find(weather->GetFormEditorID());
	if (it == pbrWeathers.end()) {
		return nullptr;
	}
	return &it->second;
}

bool State::IsPBRWeather(const RE::TESForm* weather)
{
	return GetPBRWeatherData(weather) != nullptr;
}

void State::SavePBRWeatherData(const std::string& editorId)
{
	const auto& pbrWeatherData = pbrWeathers[editorId];

	json config;
	config["directionalLightColorScale"] = pbrWeatherData.directionalLightColorScale;
	config["directionalAmbientLightColorScale"] = pbrWeatherData.directionalAmbientLightColorScale;

	PNState::SavePBRRecordConfig("Data\\PBRWeathers\\", editorId, config);
}
