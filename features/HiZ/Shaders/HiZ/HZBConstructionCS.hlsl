Texture2D<float> ParentMip : register(t0);
RWTexture2D<float> ChildMip : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint3 parentDimensions;
    ParentMip.GetDimensions(0, parentDimensions.x, parentDimensions.y, parentDimensions.z);
    
    uint2 childDimensions = max(floor(parentDimensions.xy / 2), 1);
    
    float2 uv = float2(dispatchThreadId.xy) / float2(childDimensions.xy);
    uint2 inputCoord = float2(parentDimensions.xy) * uv;
    
    float4 depth = float4(
        ParentMip.Load(int3(clamp(inputCoord + uint2(0, 0), 0, parentDimensions.xy), 0)),
        ParentMip.Load(int3(clamp(inputCoord + uint2(1, 0), 0, parentDimensions.xy), 0)),
        ParentMip.Load(int3(clamp(inputCoord + uint2(0, 1), 0, parentDimensions.xy), 0)),
        ParentMip.Load(int3(clamp(inputCoord + uint2(1, 1), 0, parentDimensions.xy), 0))
    );
    
    float maxDepth = max(depth[0], max(depth[1], max(depth[2], depth[3])));
    
    ChildMip[dispatchThreadId.xy] = maxDepth;
}