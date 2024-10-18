Texture2D<float> Src : register(t0);
RWTexture2D<float> Dst : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchThreadId
								: SV_DispatchThreadID) {
	Dst[dispatchThreadId.xy] = Src[dispatchThreadId.xy];
}