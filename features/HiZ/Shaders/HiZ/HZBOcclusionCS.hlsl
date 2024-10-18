struct BoundsData
{
	float3 center;
	float radius;
};

struct ResultData
{
	uint isOccluded;
	float3 pad;
};

cbuffer TransformData : register(b0)
{
	float4x4 ViewMatrix : packoffset(c0);
	float4x4 ProjectionMatrix : packoffset(c4);
};

Texture2D<float> HiZBuffer : register(t0);
StructuredBuffer<BoundsData> Bounds : register(t1);

RWStructuredBuffer<ResultData> Result : register(u0);

int3 ToLoadCoordinate(float x, float y, int width, int height, int mipLevel)
{
	return int3(floor(0.5 * (1 + x) * width), floor(0.5 * (1 - y) * height), mipLevel);
}

[numthreads(64, 1, 1)] void main(uint3 dispatchThreadId
								 : SV_DispatchThreadID) {
	BoundsData bounds = Bounds[dispatchThreadId.x];

	float3 centerVS = mul(ViewMatrix, float4(bounds.center, 1)).xyz;

	bool hasNegativeDepth = false;
	float minX = 0;
	float maxX = 0;
	float minY = 0;
	float maxY = 0;
	float minZ = 0;
	[unroll] for (int xSign = -1; xSign <= 1; xSign += 2)
		[unroll] for (int ySign = -1; ySign <= 1; ySign += 2)
			[unroll] for (int zSign = -1; zSign <= 1; zSign += 2)
	{
		float3 vertexVS = float3(centerVS.x + xSign * bounds.radius, centerVS.y + ySign * bounds.radius, centerVS.z + zSign * bounds.radius);
		float4 vertexCS = mul(ProjectionMatrix, float4(vertexVS, 1));
		vertexCS.xyz /= vertexCS.w;

		if (vertexCS.w < 0 || vertexCS.z < 0) {
			hasNegativeDepth = true;
		}

		if (xSign == -1 && ySign == -1 && zSign == -1) {
			minX = vertexCS.x;
			maxX = vertexCS.x;
			minY = vertexCS.y;
			maxY = vertexCS.y;
			minZ = vertexCS.z;
		} else {
			minX = min(vertexCS.x, minX);
			minY = min(vertexCS.y, minY);
			maxX = max(vertexCS.x, maxX);
			maxY = max(vertexCS.y, maxY);
			minZ = min(vertexCS.z, minZ);
		}
	}

	minX = clamp(minX, -1, 1);
	minY = clamp(minY, -1, 1);
	maxX = clamp(maxX, -1, 1);
	maxY = clamp(maxY, -1, 1);

	ResultData result;
	result.pad = 0;
	if (!hasNegativeDepth) {
		uint fullWidth, fullHeight, numLevels;
		HiZBuffer.GetDimensions(0, fullWidth, fullHeight, numLevels);

		float mipLevel = numLevels - 1 - clamp(floor(log2(2 / max(maxX - minX, maxY - minY))), 0, numLevels - 1);

		uint width, height;
		HiZBuffer.GetDimensions(mipLevel, width, height, numLevels);

		float depths[4];
		depths[0] = HiZBuffer.Load(ToLoadCoordinate(minX, minY, width, height, mipLevel));
		depths[1] = HiZBuffer.Load(ToLoadCoordinate(minX, maxY, width, height, mipLevel));
		depths[2] = HiZBuffer.Load(ToLoadCoordinate(maxX, minY, width, height, mipLevel));
		depths[3] = HiZBuffer.Load(ToLoadCoordinate(maxX, maxY, width, height, mipLevel));

		float maxDepth = max(depths[0], max(depths[1], max(depths[2], depths[3])));
		result.isOccluded = maxDepth < minZ;
	} else {
		result.isOccluded = false;
	}

	Result[dispatchThreadId.x] = result;
}