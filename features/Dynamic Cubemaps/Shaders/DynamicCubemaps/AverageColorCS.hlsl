#include "Common.hlsli"

TextureCube EnvInferredTexture : register(t0);

RWStructuredBuffer<PerPassDynamicCubemaps> AverageColor : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(1, 1, 1)] void main(uint3 ThreadID
								  : SV_DispatchThreadID) {
	uint width, height, levelCount;
	EnvInferredTexture.GetDimensions(0, width, height, levelCount);
	
	float3 averageColor = 0;
	averageColor += EnvInferredTexture.SampleLevel(LinearSampler, float3(1, 0, 0), levelCount).xyz;
	averageColor += EnvInferredTexture.SampleLevel(LinearSampler, float3(-1, 0, 0), levelCount).xyz;
	averageColor += EnvInferredTexture.SampleLevel(LinearSampler, float3(0, 1, 0), levelCount).xyz;
	averageColor += EnvInferredTexture.SampleLevel(LinearSampler, float3(0, -1, 0), levelCount).xyz;
	averageColor += EnvInferredTexture.SampleLevel(LinearSampler, float3(0, 0, 1), levelCount).xyz;
	averageColor += EnvInferredTexture.SampleLevel(LinearSampler, float3(0, 0, -1), levelCount).xyz;
	averageColor /= 6;
	
	AverageColor[0].AverageColor = averageColor;
}
