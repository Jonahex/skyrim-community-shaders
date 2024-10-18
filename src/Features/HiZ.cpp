#include "HiZ.h"

#include "Features/TerrainBlending.h"
#include "State.h"
#include "Util.h"

namespace PNHiZ
{
	uint32_t GetNextMipSize(uint32_t mipSize)
	{
		return std::max(static_cast<uint32_t>(std::floor(mipSize / 2)), 1u);
	}

	uint32_t GetMipCount(uint32_t width, uint32_t height)
	{
		return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
	}

	uint32_t CeilDivide(uint32_t x, uint32_t y)
	{
		return (x + y - 1) / y;
	}

	Util::DispatchCount GetDispatchCount(uint32_t width, uint32_t height)
	{
		constexpr uint32_t groupWidth = 8;
		constexpr uint32_t groupHeight = 8;
		return { CeilDivide(width, groupWidth), CeilDivide(height, groupHeight) };
	}

	uint32_t GetOcclusionDispatchCount(uint32_t objectCount)
	{
		constexpr uint32_t groupSize = 64;
		return CeilDivide(objectCount, groupSize);
	}

	size_t GetGlobalPassIndex(RE::BSRenderPass* pass)
	{
		static auto* renderPassCache = RE::BSRenderPassCache::GetSingleton();
		return (pass - renderPassCache[pass->cachePoolId].passes->passes) + pass->cachePoolId * RE::BSRenderPassCache::MaxPassCount;
	}

	struct Main_RenderDepth
	{
		static void thunk(bool a1, bool a2)
		{
			func(a1, a2);

			HiZ::GetSingleton()->ConstructHiZBuffer();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderWorld
	{
		static void thunk(bool a1)
		{
			HiZ::GetSingleton()->Activate();
			HiZ::GetSingleton()->CaclulateHiZBufferOcclusion();

			func(a1);

			HiZ::GetSingleton()->Deactivate();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template<uint32_t>
	struct BSBatchRenderer_RenderPassImmediately
	{
		static void thunk(RE::BSRenderPass* a_pass, uint32_t a_technique, bool a_alphaTest, uint32_t a_renderFlags)
		{
			if (!HiZ::GetSingleton()->IsRenderPassOccluded(a_pass)) {
				func(a_pass, a_technique, a_alphaTest, a_renderFlags);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void HiZ::SetupResources()
{
	auto* state = State::GetSingleton();
	auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
	const auto& depthStencilData = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN];
	depthSRV = depthStencilData.depthSRV;

	D3D11_TEXTURE2D_DESC depthStencilDesc;
	depthStencilData.texture->GetDesc(&depthStencilDesc);

	const uint32_t width = depthStencilDesc.Width;
	const uint32_t height = depthStencilDesc.Height;
	const uint32_t mipCount = PNHiZ::GetMipCount(width, height);

	mipSizes.resize(mipCount);
	mipSizes[0] = { width, height };
	for (uint32_t mipIndex = 1; mipIndex < mipCount; ++mipIndex) {
		mipSizes[mipIndex] = { PNHiZ::GetNextMipSize(mipSizes[mipIndex - 1].first), PNHiZ::GetNextMipSize(mipSizes[mipIndex - 1].second) };
	}

	{
		D3D11_TEXTURE2D_DESC hiZBufferDesc{
			.Width = width,
			.Height = height,
			.MipLevels = mipCount,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_FLOAT,
			.SampleDesc = { 1, 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0,
		};
		hiZBuffer = std::make_unique<Texture2D>(hiZBufferDesc);
		{
			const auto name = "Hi-Z Buffer"sv;
			hiZBuffer->resource->SetPrivateData(WKPDID_D3DDebugObjectName,
				static_cast<UINT>(name.size()), name.data());
		}

		hzbSrvs.resize(mipCount);
		for (uint32_t srvIndex = 0; srvIndex < mipCount; ++srvIndex) {
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
				.Format = DXGI_FORMAT_R32_FLOAT,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = srvIndex,
					.MipLevels = 1,
				},
			};
			hiZBuffer->CreateSRV(srvDesc);
			hzbSrvs[srvIndex] = hiZBuffer->srv;
			const auto name = std::format("Hi-Z Buffer Mip {} SRV", srvIndex);
			hiZBuffer->srv->SetPrivateData(WKPDID_D3DDebugObjectName,
				static_cast<UINT>(name.size()), name.data());
		}
		hiZBuffer->srv = {};

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_R32_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = mipCount,
			},
		};
		hiZBuffer->CreateSRV(srvDesc);
		{
			const auto name = "Hi-Z Buffer SRV"sv;
			hiZBuffer->srv->SetPrivateData(WKPDID_D3DDebugObjectName,
				static_cast<UINT>(name.size()), name.data());
		}

		hzbUavs.resize(mipCount);
		for (uint32_t uavIndex = 0; uavIndex < mipCount; ++uavIndex) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{
				.Format = DXGI_FORMAT_R32_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = uavIndex },
			};
			hiZBuffer->CreateUAV(uavDesc);
			hzbUavs[uavIndex] = hiZBuffer->uav;
			const auto name = std::format("Hi-Z Buffer Mip {} UAV", uavIndex);
			hiZBuffer->uav->SetPrivateData(WKPDID_D3DDebugObjectName,
				static_cast<UINT>(name.size()), name.data());
		}
		hiZBuffer->uav = {};
	}

	{
		D3D11_BUFFER_DESC boundsBufferDesc{
			.ByteWidth = MaxSimultaneousRenderPassCount * sizeof(BoundsData),
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
			.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			.StructureByteStride = sizeof(BoundsData),
		};
		boundsData = std::make_unique<Buffer>(boundsBufferDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = MaxSimultaneousRenderPassCount },
		};
		boundsData->CreateSRV(srvDesc);
	}

	{
		cameraData = std::make_unique<ConstantBuffer>(ConstantBufferDesc<CameraData>());
	}

	{
		D3D11_BUFFER_DESC resultBufferDesc{
			.ByteWidth = MaxSimultaneousRenderPassCount * sizeof(ResultData),
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			.StructureByteStride = sizeof(ResultData),
		};
		resultData = std::make_unique<Buffer>(resultBufferDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = MaxSimultaneousRenderPassCount, .Flags = 0 },
		};
		resultData->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC resultBufferDesc{
			.ByteWidth = MaxSimultaneousRenderPassCount * sizeof(ResultData),
			.Usage = D3D11_USAGE_STAGING,
			.BindFlags = 0,
			.CPUAccessFlags = D3D11_CPU_ACCESS_READ,
			.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			.StructureByteStride = sizeof(ResultData),
		};
		resultReadback = std::make_unique<Buffer>(resultBufferDesc);
	}

	{
		D3D11_QUERY_DESC readbackQueryDesc = {
			.Query = D3D11_QUERY_EVENT,
			.MiscFlags = 0
		};

		ID3D11Query* query = nullptr;
		state->device->CreateQuery(&readbackQueryDesc, &query);
		readbackQuery.attach(query);
	}

	if (auto* shader = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HiZ\\CopyDepthCS.hlsl", {}, "cs_5_0"))) {
		copyDepthCS.attach(shader);
	}
	if (auto* shader = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HiZ\\HZBConstructionCS.hlsl", {}, "cs_5_0"))) {
		hzbConstructionCS.attach(shader);
	}
	if (auto* shader = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HiZ\\HZBOcclusionCS.hlsl", {}, "cs_5_0"))) {
		hzbOcclusionCS.attach(shader);
	}

	passMap.resize(MaxRenderPassCount);
	std::fill(passMap.begin(), passMap.end(), -1);

	results.resize(MaxSimultaneousRenderPassCount);
}

void HiZ::ConstructHiZBuffer()
{
	if (!isEnabled)
	{
		return;
	}

	auto* state = State::GetSingleton();
	auto* context = state->context;

	state->BeginPerfEvent("Hi-Z Buffer Construction");

	context->OMSetRenderTargets(0, nullptr, nullptr);

	{
		context->CSSetShader(copyDepthCS.get(), nullptr, 0);

		ID3D11ShaderResourceView* srvs[1] = { depthSRV };
		context->CSSetShaderResources(0, 1, srvs);

		ID3D11UnorderedAccessView* uavs[1] = { hzbUavs[0].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		const auto dispatchCount = PNHiZ::GetDispatchCount(mipSizes[0].first, mipSizes[0].second);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	context->CSSetShader(hzbConstructionCS.get(), nullptr, 0);

	for (uint32_t mipIndex = 1; mipIndex < mipSizes.size(); ++mipIndex) {
		ID3D11UnorderedAccessView* uavs[1] = { hzbUavs[mipIndex].get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		ID3D11ShaderResourceView* srvs[1] = { hzbSrvs[mipIndex - 1].get() };
		context->CSSetShaderResources(0, 1, srvs);

		const auto dispatchCount = PNHiZ::GetDispatchCount(mipSizes[mipIndex].first, mipSizes[mipIndex].second);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	{
		ID3D11ShaderResourceView* srvs[1] = { nullptr };
		context->CSSetShaderResources(0, 1, srvs);
	}
	{
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}
	context->CSSetShader(nullptr, nullptr, 0);

	state->EndPerfEvent();
}

void HiZ::CaclulateHiZBufferOcclusion()
{
	if (!isActive) {
		return;
	}

	static auto* accumulator = *reinterpret_cast<RE::BSGraphics::BSShaderAccumulator**>(REL::RelocationID(528064, 415009).address());

	auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
	auto* state = State::GetSingleton();
	auto* context = state->context;

	if (hasPrevFrameData) {
		D3D11_MAPPED_SUBRESOURCE mappedResult;
		context->Map(resultReadback->resource.get(), 0, D3D11_MAP_READ, 0, &mappedResult);
		std::memcpy(results.data(), mappedResult.pData, sizeof(ResultData) * processedRenderPassesLastFrame);
		context->Unmap(resultReadback->resource.get(), 0);

		for (uint32_t resultIndex = 0; resultIndex < processedRenderPassesLastFrame; ++resultIndex) {
			if (results[resultIndex].result) {
				++occludedRenderPassesLastFrame;
			}
		}
		std::swap(prevGeometryMap, geometryMap);
	}

	std::fill(passMap.begin(), passMap.end(), -1);
	geometryMap.clear();

	D3D11_MAPPED_SUBRESOURCE mappedBoundsData;
	DX::ThrowIfFailed(context->Map(boundsData->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBoundsData));

	auto* occlusionData = reinterpret_cast<BoundsData*>(mappedBoundsData.pData);

	int32_t currentIndex = 0;

	auto isOccludable = [](RE::BSRenderPass* pass) {
		if (pass == nullptr || pass->geometry == nullptr || pass->shader == nullptr || pass->shaderProperty == nullptr) {
			return false;
		}
		auto* shader = pass->shader;
		if (shader->shaderType == RE::BSShader::Type::Grass || shader->shaderType == RE::BSShader::Type::Sky) {
			return false;
		}
		auto* geometry = pass->geometry;
		if (geometry->GetGeometryRuntimeData().skinInstance != nullptr) {
			return false;
		}
		auto* shaderProperty = pass->shaderProperty;
		if (shaderProperty->material == nullptr || shaderProperty->material->GetFeature() == RE::BSShaderMaterial::Feature::kTreeAnim) {
			return false;
		}
		return true;
	};

	auto addPass = [&](RE::BSRenderPass* pass) {
		if (currentIndex < MaxSimultaneousRenderPassCount && isOccludable(pass)) {
			/*const size_t globalPassIndex = PNHiZ::GetGlobalPassIndex(pass);
			if (passMap[globalPassIndex] == -1) {
				passMap[globalPassIndex] = currentIndex;

				auto bound = pass->geometry->worldBound;
				bound.center -= shadowState->GetRuntimeData().posAdjust.getEye(0);
				occlusionData[currentIndex].bound = bound;

				++currentIndex;
			}*/

			geometryMap.insert_or_assign(pass->geometry, currentIndex);

			auto bound = pass->geometry->worldBound;
			bound.center -= shadowState->GetRuntimeData().posAdjust.getEye(0);
			occlusionData[currentIndex].bound = bound;

			++currentIndex;
		}
	};

	auto isOccluded = [&](RE::BSRenderPass* pass) {
		if (isOccludable(pass)) {
			/*const size_t globalPassIndex = PNHiZ::GetGlobalPassIndex(pass);
			const int32_t resultIndex = passMap[globalPassIndex];
			if (resultIndex != -1) {
				if (results[resultIndex].result) {
					return true;
				}
			}*/
			if (auto it = prevGeometryMap.find(pass->geometry); it != prevGeometryMap.end()) {
				if (results[it->second].result) {
					return true;
				}
			}
		}
		return false;
	};

	auto removePasses = [&](RE::BSRenderPass*& firstPass) {
		RE::BSRenderPass* prevPass = nullptr;
		auto* pass = firstPass;
		while (pass) {
			auto* nextPass = pass->passGroupNext;
			if (isOccluded(pass)) {
				if (prevPass != nullptr) {
					prevPass->passGroupNext = nextPass;
				}
				pass->passGroupNext = nullptr;
			} else {
				if (prevPass == nullptr) {
					firstPass = pass;
				}
				prevPass = pass;
			}
			pass = nextPass;
		}
		if (prevPass == nullptr) {
			firstPass = nullptr;
		}
	};

	auto* batchRenderer = accumulator->GetRuntimeData().batchRenderer;
	for (const auto& passGroup : batchRenderer->passGroups)
	{
		for (size_t bucketIndex = 0; bucketIndex < 5; ++bucketIndex) {
			if ((passGroup.validBuckets & (1 << bucketIndex)) != 0) {
				for (auto* pass = passGroup.passes[bucketIndex]; pass; pass = pass->passGroupNext) {
					addPass(pass);
				}
			}
		}
	}
	for (auto* geometryGroup : batchRenderer->geometryGroups) {
		for (auto* pass = geometryGroup->passList.head; pass; pass = pass->passGroupNext) {
			addPass(pass);
		}
	}

	context->Unmap(boundsData->resource.get(), 0);
	
	const CameraData newCameraData{
		.viewMatrix = Util::GetCameraData(0).viewMat,
		.projectionMatrix = Util::GetCameraData(0).projMat,
	};
	cameraData->Update(newCameraData);

	context->CSSetShader(hzbOcclusionCS.get(), nullptr, 0);
	{
		ID3D11ShaderResourceView* srvs[2] = { hiZBuffer->srv.get(), boundsData->srv.get() };
		context->CSSetShaderResources(0, 2, srvs);
	}
	{
		ID3D11Buffer* buffers[1] = { cameraData->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);
	}
	{
		ID3D11UnorderedAccessView* uavs[1] = { resultData->uav.get() };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}

	context->Dispatch(PNHiZ::GetOcclusionDispatchCount(currentIndex), 1, 1);

	{
		ID3D11ShaderResourceView* srvs[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, 2, srvs);
	}
	{
		ID3D11Buffer* buffers[1] = { nullptr };
		context->CSSetConstantBuffers(0, 1, buffers);
	}
	{
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	}
	context->CSSetShader(nullptr, nullptr, 0);

	context->CopyResource(resultReadback->resource.get() , resultData->resource.get());
	//context->Flush();
	//context->End(readbackQuery.get());
	isReadbackReady = false;
	hasPrevFrameData = true;

	processedRenderPassesLastFrame = static_cast<uint32_t>(currentIndex);

	/*D3D11_MAPPED_SUBRESOURCE mappedResult;
	context->Map(resultReadback->resource.get(), 0, D3D11_MAP_READ, 0, &mappedResult);
	std::memcpy(results.data(), mappedResult.pData, sizeof(ResultData) * processedRenderPassesLastFrame);
	context->Unmap(resultReadback->resource.get(), 0);

	for (uint32_t resultIndex = 0; resultIndex < processedRenderPassesLastFrame; ++resultIndex) {
		if (results[resultIndex].result) {
			++occludedRenderPassesLastFrame;
		}
	}*/

	/*for (auto& passGroup : batchRenderer->passGroups) {
		for (size_t bucketIndex = 0; bucketIndex < 5; ++bucketIndex) {
			if ((passGroup.validBuckets & (1 << bucketIndex)) != 0) {
				removePasses(passGroup.passes[bucketIndex]);
				if (passGroup.passes[bucketIndex] == nullptr) {
					passGroup.validBuckets &= ~(1 << bucketIndex);
				}
			}
		}
	}*/
	/*{
		auto* node = batchRenderer->activePassIndexList.get_head();
		RE::BSSimpleList<uint32_t>::Node* prevNode = nullptr;
		while (node) {
			auto* nextNode = node->next;
			bool removed = false;
			if (auto passGroupIndexIt = batchRenderer->techniqueToPassGroupIndex.find(node->item); passGroupIndexIt != batchRenderer->techniqueToPassGroupIndex.end()) {
				if (batchRenderer->passGroups[passGroupIndexIt->second].validBuckets == 0) {
					removed = true;
					if (prevNode != nullptr) {
						prevNode->next = nextNode;
					}
					delete node;
				}
			}
			if (!removed) {
				if (prevNode == nullptr) {
					batchRenderer->activePassIndexList._listHead = node;
				}
				prevNode = node;
			}
		}
	}*/
	/*auto prevIt = batchRenderer->activePassIndexList.cbegin();
	for (auto techniqueIt = std::next(batchRenderer->activePassIndexList.begin()); techniqueIt != batchRenderer->activePassIndexList.end();) {
		bool removed = false;
		if (auto passGroupIndexIt = batchRenderer->techniqueToPassGroupIndex.find(*techniqueIt); passGroupIndexIt != batchRenderer->techniqueToPassGroupIndex.end()) {
			if (batchRenderer->passGroups[passGroupIndexIt->second].validBuckets == 0) {
				techniqueIt = batchRenderer->activePassIndexList.erase_after(prevIt);
				removed = true;
			}
		}
		if (!removed) {
			++prevIt;
			++techniqueIt;
		}
	}
	if (!batchRenderer->activePassIndexList.empty()) {
		if (auto passGroupIndexIt = batchRenderer->techniqueToPassGroupIndex.find(batchRenderer->activePassIndexList.front()); passGroupIndexIt != batchRenderer->techniqueToPassGroupIndex.end()) {
			if (batchRenderer->passGroups[passGroupIndexIt->second].validBuckets == 0) {
				batchRenderer->activePassIndexList.pop_front();
			}
		}
	}*/
	/*for (auto* geometryGroup : batchRenderer->geometryGroups) {
		removePasses(geometryGroup->passList.head);
	}*/
}

void HiZ::Activate()
{
	occludedRenderPassesLastFrame = 0;
	//processedRenderPassesLastFrame = 0;
	skippedRenderPassesLastFrame = 0;

	if (!isEnabled) {
		return;
	}

	isActive = true;
}

void HiZ::Deactivate()
{
	if (!isActive) {
		return;
	}

	isActive = false;
}

bool HiZ::IsRenderPassOccluded(RE::BSRenderPass* pass)
{
	if (isActive) {
		/*if (!isReadbackReady) {
			auto* state = State::GetSingleton();
			auto* context = state->context;
			if (context->GetData(readbackQuery.get(), nullptr, 0, 0) == S_OK) {
				isReadbackReady = true;
				D3D11_MAPPED_SUBRESOURCE mappedResult;
				context->Map(resultReadback->resource.get(), 0, D3D11_MAP_READ, 0, &mappedResult);
				std::memcpy(results.data(), mappedResult.pData, sizeof(ResultData) * processedRenderPassesLastFrame);
				context->Unmap(resultReadback->resource.get(), 0);

				for (uint32_t resultIndex = 0; resultIndex < processedRenderPassesLastFrame; ++resultIndex) {
					if (results[resultIndex].result) {
						++occludedRenderPassesLastFrame;
					}
				}
			} else {
				return false;
			}
		}*/

		/*const size_t globalPassIndex = PNHiZ::GetGlobalPassIndex(pass);
		const int32_t resultIndex = passMap[globalPassIndex];
		if (resultIndex != -1) {
			if (results[resultIndex].result) {
				++skippedRenderPassesLastFrame;
				return true;
			}
		}*/
		if (auto it = prevGeometryMap.find(pass->geometry); it != prevGeometryMap.end())
		{
			if (results[it->second].result) {
				++skippedRenderPassesLastFrame;
				return true;
			}
		}
	}
	return false;
}

void HiZ::PostPostLoad()
{
	stl::write_thunk_call<PNHiZ::Main_RenderDepth>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x395, 0x395, 0x2EE));
	stl::write_thunk_call<PNHiZ::Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841, 0x791));
	stl::write_thunk_call<PNHiZ::BSBatchRenderer_RenderPassImmediately<100877>>(REL::RelocationID(100877, 107673).address() + REL::Relocate(0x1E5, 0x1EE));
	stl::write_thunk_call<PNHiZ::BSBatchRenderer_RenderPassImmediately<100852>>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));
	stl::write_thunk_call<PNHiZ::BSBatchRenderer_RenderPassImmediately<100871>>(REL::RelocationID(100871, 107667).address() + REL::Relocate(0xEE, 0xED));
}

void HiZ::DrawSettings()
{
	ImGui::Checkbox("Enabled", &isEnabled);
	if (isEnabled) {
		ImGui::Text("Occluded %d/%d render passes last frame (actually skipped %d)", occludedRenderPassesLastFrame, processedRenderPassesLastFrame, skippedRenderPassesLastFrame);
	}
}