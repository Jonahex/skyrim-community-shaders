#pragma once

#include "Buffer.h"
#include "Feature.h"

struct HiZ : public Feature
{
	struct BoundsData
	{
		RE::NiBound bound;
	};

	struct CameraData
	{
		DirectX::SimpleMath::Matrix viewMatrix;
		DirectX::SimpleMath::Matrix projectionMatrix;
	};

	struct ResultData
	{
		uint32_t result = 0;
		float pad[3];
	};

	static HiZ* GetSingleton()
	{
		static HiZ singleton;
		return std::addressof(singleton);
	}

	virtual inline std::string GetName() override { return "Hi-Z Occlusion"; }
	virtual inline std::string GetShortName() override { return "HiZ"; }

	virtual void SetupResources() override;
	virtual void PostPostLoad() override;
	virtual void DrawSettings() override;

	void Activate();
	void Deactivate();
	void ConstructHiZBuffer();
	void CaclulateHiZBufferOcclusion();
	bool IsRenderPassOccluded(RE::BSRenderPass* pass);

	static constexpr size_t MaxRenderPassCount = RE::BSRenderPassCache::CacheCount * RE::BSRenderPassCache::MaxPassCount;
	static constexpr size_t MaxSimultaneousRenderPassCount = 8192;

	bool isEnabled = true;
	bool isActive = false;
	bool isReadbackReady = false;
	bool hasPrevFrameData = false;

	uint32_t occludedRenderPassesLastFrame = 0;
	uint32_t processedRenderPassesLastFrame = 0;
	uint32_t skippedRenderPassesLastFrame = 0;

	ID3D11ShaderResourceView* depthSRV = nullptr;

	std::unique_ptr<Texture2D> hiZBuffer;
	std::vector<std::pair<uint32_t, uint32_t>> mipSizes;
	std::vector<winrt::com_ptr<ID3D11ShaderResourceView>> hzbSrvs;
	std::vector<winrt::com_ptr<ID3D11UnorderedAccessView>> hzbUavs;

	std::unique_ptr<Buffer> boundsData;

	std::unique_ptr<ConstantBuffer> cameraData;

	std::unique_ptr<Buffer> resultData;
	std::unique_ptr<Buffer> resultReadback;

	winrt::com_ptr<ID3D11Query> readbackQuery;

	winrt::com_ptr<ID3D11ComputeShader> copyDepthCS;
	winrt::com_ptr<ID3D11ComputeShader> hzbConstructionCS;
	winrt::com_ptr<ID3D11ComputeShader> hzbOcclusionCS;

	std::vector<int32_t> passMap;
	std::vector<ResultData> results;
	std::unordered_map<RE::BSGeometry*, uint32_t> geometryMap;
	std::unordered_map<RE::BSGeometry*, uint32_t> prevGeometryMap;
};