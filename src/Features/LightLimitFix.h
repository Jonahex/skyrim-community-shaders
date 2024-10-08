#pragma once
#include <DirectXMath.h>
#include <d3d11.h>

#include "Buffer.h"
#include "Util.h"
#include <shared_mutex>

#include "Feature.h"
#include "ShaderCache.h"
#include <Features/LightLimitFix/ParticleLights.h>

struct LightLimitFix : Feature
{
public:
	static LightLimitFix* GetSingleton()
	{
		static LightLimitFix render;
		return &render;
	}

	virtual inline std::string GetName() override { return "Light Limit Fix"; }
	virtual inline std::string GetShortName() override { return "LightLimitFix"; }
	virtual inline std::string_view GetShaderDefineName() override { return "LIGHT_LIMIT_FIX"; }

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	constexpr static uint32_t MaxRoomsCount = 128;
	constexpr static uint32_t MaxShadowmapsCount = 128;

	enum class LightFlags : std::uint32_t
	{
		PortalStrict = (1 << 0),
		Shadow = (1 << 1),
	};

	struct PositionOpt
	{
		float3 data;
		uint pad0;
	};

	struct alignas(16) LightData
	{
		float3 color;
		float radius;
		PositionOpt positionWS[2];
		PositionOpt positionVS[2];
		std::bitset<MaxRoomsCount> roomFlags;
		stl::enumeration<LightFlags> lightFlags;
		uint32_t shadowMaskIndex = 0;
		float pad0[2];
	};

	struct ClusterAABB
	{
		float4 minPoint;
		float4 maxPoint;
	};

	struct alignas(16) LightGrid
	{
		uint offset;
		uint lightCount;
		uint pad0[2];
	};

	struct alignas(16) LightBuildingCB
	{
		float4x4 InvProjMatrix[2];
		float LightsNear;
		float LightsFar;
		uint pad0[2];
	};

	struct alignas(16) LightCullingCB
	{
		uint LightCount;
		uint pad[3];
	};

	struct alignas(16) PerFrame
	{
		uint EnableContactShadows;
		uint EnableLightsVisualisation;
		uint LightsVisualisationMode;
		float pad0;
		uint ClusterSize[4];
	};

	PerFrame GetCommonBufferData();

	struct alignas(16) StrictLightData
	{
		LightData StrictLights[15];
		uint NumStrictLights;
		int RoomIndex;
		uint pad0[2];
	};

	StrictLightData strictLightDataTemp;

	struct CachedParticleLight
	{
		float grey;
		RE::NiPoint3 position;
		float radius;
	};

	std::unique_ptr<Buffer> strictLightData = nullptr;

	int eyeCount = !REL::Module::IsVR() ? 1 : 2;

	ID3D11ComputeShader* clusterBuildingCS = nullptr;
	ID3D11ComputeShader* clusterCullingCS = nullptr;

	ConstantBuffer* lightBuildingCB = nullptr;
	ConstantBuffer* lightCullingCB = nullptr;

	eastl::unique_ptr<Buffer> lights = nullptr;
	eastl::unique_ptr<Buffer> clusters = nullptr;
	eastl::unique_ptr<Buffer> lightCounter = nullptr;
	eastl::unique_ptr<Buffer> lightList = nullptr;
	eastl::unique_ptr<Buffer> lightGrid = nullptr;

	std::uint32_t lightCount = 0;
	float lightsNear = 1;
	float lightsFar = 16384;

	struct ParticleLightInfo
	{
		RE::NiColorA color;
		ParticleLights::Config& config;
	};

	eastl::hash_map<RE::BSGeometry*, ParticleLightInfo> queuedParticleLights;
	eastl::hash_map<RE::BSGeometry*, ParticleLightInfo> particleLights;

	RE::NiPoint3 eyePositionCached[2]{};
	Matrix viewMatrixCached[2]{};
	Matrix viewMatrixInverseCached[2]{};

	virtual void SetupResources() override;
	virtual void Reset() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void DrawSettings() override;

	virtual void PostPostLoad() override;
	virtual void DataLoaded() override;

	float CalculateLightDistance(float3 a_lightPosition, float a_radius);
	void AddCachedParticleLights(eastl::vector<LightData>& lightsData, LightLimitFix::LightData& light);
	void SetLightPosition(LightLimitFix::LightData& a_light, RE::NiPoint3 a_initialPosition, bool a_cached = true);
	void UpdateLights();
	virtual void Prepass() override;

	static inline float3 Saturation(float3 color, float saturation);
	static inline bool IsValidLight(RE::BSLight* a_light);
	static inline bool IsGlobalLight(RE::BSLight* a_light);

	struct Settings
	{
		bool EnableContactShadows = false;
		bool EnableLightsVisualisation = false;
		uint LightsVisualisationMode = 0;
		bool EnableParticleLights = true;
		bool EnableParticleLightsCulling = true;
		bool EnableParticleLightsDetection = true;
		float ParticleLightsSaturation = 1.0f;
		float ParticleBrightness = 1.0f;
		float ParticleRadius = 1.0f;
		float BillboardBrightness = 1.0f;
		float BillboardRadius = 1.0f;
		bool EnableParticleLightsOptimization = true;
		uint ParticleLightsOptimisationClusterRadius = 32;
	};

	uint clusterSize[3] = { 16 };

	Settings settings;

	using ConfigPair = std::pair<ParticleLights::Config*, ParticleLights::GradientConfig*>;
	std::optional<ConfigPair> GetParticleLightConfigs(RE::BSRenderPass* a_pass);
	bool AddParticleLight(RE::BSRenderPass* a_pass, ConfigPair a_config);
	bool CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique);

	void BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* a_pass);

	enum class Space
	{
		World = 0,
		Model = 1,
	};

	void BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(RE::BSRenderPass* a_pass, DirectX::XMMATRIX& Transform, uint32_t, uint32_t, float WorldScale, Space RenderSpace);

	void BSLightingShader_SetupGeometry_After(RE::BSRenderPass* a_pass);

	std::shared_mutex cachedParticleLightsMutex;
	eastl::vector<CachedParticleLight> cachedParticleLights;
	std::uint32_t particleLightsDetectionHits = 0;

	eastl::hash_map<RE::NiNode*, uint8_t> roomNodes;

	std::bitset<MaxShadowmapsCount> usedShadowmaps;
	RE::BSShadowLight::ShadowmapDescriptor* currentShadowmapDescriptor = nullptr;
	std::array<ID3D11DepthStencilView*, MaxShadowmapsCount> shadowmapViews;
	std::array<ID3D11DepthStencilView*, MaxShadowmapsCount> shadowmapReadOnlyViews;

	float CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point);
	void AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel);

	void CreateDepthStencilTarget(RE::BSGraphics::RenderTargetManager& targetManager,
		RE::RENDER_TARGET_DEPTHSTENCIL target,
		RE::BSGraphics::DepthStencilTargetProperties& targetProperties);
	void SetupShadowmapRenderTarget(bool isComputeShader);

	struct Hooks
	{
		struct ValidLight1
		{
			static bool thunk(RE::BSShaderProperty* a_property, RE::BSLight* a_light)
			{
				return func(a_property, a_light) && ((REL::Module::IsVR() && !netimmerse_cast<RE::BSLightingShaderProperty*>(a_property)) || (a_light->portalStrict || !a_light->portalGraph || skyrim_cast<RE::BSShadowLight*>(a_light)));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ValidLight2
		{
			static bool thunk(RE::BSShaderProperty* a_property, RE::BSLight* a_light)
			{
				return func(a_property, a_light) && ((REL::Module::IsVR() && !netimmerse_cast<RE::BSLightingShaderProperty*>(a_property)) || (a_light->portalStrict || !a_light->portalGraph || skyrim_cast<RE::BSShadowLight*>(a_light)));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ValidLight3
		{
			static bool thunk(RE::BSShaderProperty* a_property, RE::BSLight* a_light)
			{
				return func(a_property, a_light) && ((REL::Module::IsVR() && !netimmerse_cast<RE::BSLightingShaderProperty*>(a_property)) || (a_light->portalStrict || !a_light->portalGraph || skyrim_cast<RE::BSShadowLight*>(a_light)));
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately1
		{
			static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
			{
				if (GetSingleton()->CheckParticleLights(Pass, Technique))
					func(Pass, Technique, AlphaTest, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately2
		{
			static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
			{
				if (GetSingleton()->CheckParticleLights(Pass, Technique))
					func(Pass, Technique, AlphaTest, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately3
		{
			static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
			{
				if (GetSingleton()->CheckParticleLights(Pass, Technique))
					func(Pass, Technique, AlphaTest, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				GetSingleton()->BSLightingShader_SetupGeometry_Before(Pass);
				func(This, Pass, RenderFlags);
				GetSingleton()->BSLightingShader_SetupGeometry_After(Pass);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSEffectShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				func(This, Pass, RenderFlags);
				GetSingleton()->BSLightingShader_SetupGeometry_Before(Pass);
				GetSingleton()->BSLightingShader_SetupGeometry_After(Pass);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSWaterShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				func(This, Pass, RenderFlags);
				GetSingleton()->BSLightingShader_SetupGeometry_Before(Pass);
				GetSingleton()->BSLightingShader_SetupGeometry_After(Pass);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct AIProcess_CalculateLightValue_GetLuminance
		{
			static float thunk(RE::ShadowSceneNode* shadowSceneNode, RE::NiPoint3& targetPosition, int& numHits, float& sunLightLevel, float& lightLevel, RE::NiLight& refLight, int32_t shadowBitMask)
			{
				auto ret = func(shadowSceneNode, targetPosition, numHits, sunLightLevel, lightLevel, refLight, shadowBitMask);
				GetSingleton()->AddParticleLightLuminance(targetPosition, numHits, ret);
				return ret;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights
		{
			static void thunk(RE::BSGraphics::PixelShader* PixelShader, RE::BSRenderPass* Pass, DirectX::XMMATRIX& Transform, uint32_t LightCount, uint32_t ShadowLightCount, float WorldScale, Space RenderSpace)
			{
				GetSingleton()->BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights(Pass, Transform, LightCount, ShadowLightCount, WorldScale, RenderSpace);
				func(PixelShader, Pass, Transform, LightCount, ShadowLightCount, WorldScale, RenderSpace);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSLightingShaderProperty_GetRenderPasses
		{
			static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
			{
				auto renderPasses = func(property, geometry, renderFlags, accumulator);
				if (renderPasses == nullptr) {
					return renderPasses;
				}

				auto currentPass = renderPasses->head;
				while (currentPass != nullptr) {
					if (currentPass->shader->shaderType == RE::BSShader::Type::Lighting) {
						constexpr uint32_t LightingTechniqueStart = 0x4800002D;
						// So that we always have shadow mask bound.
						currentPass->passEnum = ((currentPass->passEnum - LightingTechniqueStart) | static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::DefShadow)) + LightingTechniqueStart;
					}
					currentPass = currentPass->next;
				}

				return renderPasses;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct RenderTargetManager__CreateDepthStencilTarget
		{
			static void thunk(RE::BSGraphics::RenderTargetManager* targetManager,
				RE::RENDER_TARGET_DEPTHSTENCIL target,
				RE::BSGraphics::DepthStencilTargetProperties* targetProperties)
			{
				GetSingleton()->CreateDepthStencilTarget(*targetManager, target, *targetProperties);
			}
		};

		struct BSShadowLight__RenderShadowmap
		{
			static void thunk(RE::BSShadowLight* light,
				RE::BSShadowLight::ShadowmapDescriptor* shadowmapDescriptor,
				int* a3,
				int a4)
			{
				if (shadowmapDescriptor->renderTarget == RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE) {
					GetSingleton()->currentShadowmapDescriptor = shadowmapDescriptor;
				}
				func(light, shadowmapDescriptor, a3, a4);
				GetSingleton()->currentShadowmapDescriptor = nullptr;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShadowLight__RenderShadowmap__SetClearColor
		{
			static void thunk(RE::BSGraphics::Renderer* renderer, float a2, float a3, float a4, int a5)
			{
				if (GetSingleton()->currentShadowmapDescriptor != nullptr) {
					auto& usedShadowmaps = GetSingleton()->usedShadowmaps;
					for (uint32_t shadowmapIndex = 0; shadowmapIndex < usedShadowmaps.size(); ++shadowmapIndex) {
						if (!usedShadowmaps[shadowmapIndex]) {
							usedShadowmaps.set(shadowmapIndex);
							GetSingleton()->currentShadowmapDescriptor->shadowmapIndex = shadowmapIndex;
							break;
						}
					}
				}

				func(renderer, a2, a3, a4, a5);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSShadowLight__Reset
		{
			static void thunk(RE::BSShadowLight* light)
			{
				for (uint32_t shadowmapIndex = 0; shadowmapIndex < light->shadowMapCount; ++shadowmapIndex) {
					auto& descriptor = light->shadowmapDescriptors[shadowmapIndex];
					if (descriptor.renderTarget != RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE) {
						GetSingleton()->usedShadowmaps.reset(descriptor.shadowmapIndex);
						descriptor.renderTarget = RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE;
						descriptor.unkCC = 1.f;
					}
				}
			}
		};

		struct BSShadowLight__ResetShadowmap
		{
			static void thunk(RE::BSShadowLight* light, uint32_t shadowmapIndex)
			{
				auto& descriptor = light->shadowmapDescriptors[shadowmapIndex];
				if (descriptor.renderTarget != RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE) {
					GetSingleton()->usedShadowmaps.reset(descriptor.shadowmapIndex);

					func(light, shadowmapIndex);
					/*descriptor.renderTarget = RE::RENDER_TARGETS_DEPTHSTENCIL::kNONE;
					descriptor.shaderAccumulator = nullptr;
					descriptor.camera = nullptr;
					if (descriptor.cullingProcess != light->cullingProcess) {
						if (descriptor.cullingProcess != nullptr) {
							descriptor.cullingProcess->~BSCullingProcess();
						}
						descriptor.cullingProcess = nullptr;
					}*/
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer__SetDirtyRenderTargets
		{
			static void thunk(RE::BSGraphics::Renderer* renderer, bool isComputeShader)
			{
				const bool dirtyRt = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().stateUpdateFlags.any(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
				GetSingleton()->SetupShadowmapRenderTarget(isComputeShader);
				func(renderer, isComputeShader);
				if (dirtyRt) {
					RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_thunk_call<ValidLight1>(REL::RelocationID(100994, 107781).address() + 0x92);
			stl::write_thunk_call<ValidLight2>(REL::RelocationID(100997, 107784).address() + REL::Relocate(0x139, 0x12A));
			stl::write_thunk_call<ValidLight3>(REL::RelocationID(101296, 108283).address() + REL::Relocate(0xB7, 0x7E));

			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately1>(REL::RelocationID(100877, 107673).address() + REL::Relocate(0x1E5, 0x1EE));
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately2>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately3>(REL::RelocationID(100871, 107667).address() + REL::Relocate(0xEE, 0xED));

			stl::write_thunk_call<AIProcess_CalculateLightValue_GetLuminance>(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1C9, 0x1D3));

			stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);

			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
			stl::write_vfunc<0x6, BSEffectShader_SetupGeometry>(RE::VTABLE_BSEffectShader[0]);
			stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);

			stl::detour_thunk_ignore_func<RenderTargetManager__CreateDepthStencilTarget>(REL::RelocationID(75639, 77446));
			stl::detour_thunk<BSShadowLight__RenderShadowmap>(REL::RelocationID(100820, 107604));
			stl::write_thunk_call<BSShadowLight__RenderShadowmap__SetClearColor>(REL::RelocationID(100820, 107604).address() + REL::Relocate(0x101, 0x103, 0x126));
			stl::detour_thunk_ignore_func<BSShadowLight__Reset>(REL::RelocationID(100816, 107600));
			stl::detour_thunk<BSShadowLight__ResetShadowmap>(REL::RelocationID(100814, 107598));
			stl::detour_thunk<Renderer__SetDirtyRenderTargets>(REL::RelocationID(75462, 77247));

			logger::info("[LLF] Installed hooks");

			stl::write_thunk_call<BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights>(REL::RelocationID(100565, 107300).address() + REL::Relocate(0x523, 0xB0E, 0x5fe));
		}
	};

	virtual bool SupportsVR() override { return true; };
};

template <>
struct fmt::formatter<LightLimitFix::LightData>
{
	// Presentation format: 'f' - fixed.
	char presentation = 'f';

	// Parses format specifications of the form ['f'].
	constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator
	{
		auto it = ctx.begin(), end = ctx.end();
		if (it != end && (*it == 'f'))
			presentation = *it++;

		// Check if reached the end of the range:
		if (it != end && *it != '}')
			throw_format_error("invalid format");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	// Formats the point p using the parsed format specification (presentation)
	// stored in this formatter.
	auto format(const LightLimitFix::LightData& l, format_context& ctx) const -> format_context::iterator
	{
		// ctx.out() is an output iterator to write to.
		return fmt::format_to(ctx.out(), "{{address {:x} color {} radius {} posWS {} {} posVS {} {}}}",
			reinterpret_cast<uintptr_t>(&l),
			(Vector3)l.color,
			l.radius,
			(Vector3)l.positionWS[0].data, (Vector3)l.positionWS[1].data,
			(Vector3)l.positionVS[0].data, (Vector3)l.positionVS[1].data);
	}
};