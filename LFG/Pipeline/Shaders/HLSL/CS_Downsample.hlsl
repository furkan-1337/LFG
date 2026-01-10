Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Simple 2x2 box filter downsampling
    uint2 srcPos = dispatchThreadId.xy * 2;
    
    float4 c0 = Input[srcPos + uint2(0, 0)];
    float4 c1 = Input[srcPos + uint2(1, 0)];
    float4 c2 = Input[srcPos + uint2(0, 1)];
    float4 c3 = Input[srcPos + uint2(1, 1)];
    
    float4 average = (c0 + c1 + c2 + c3) * 0.25f;
    
    Output[dispatchThreadId.xy] = average;
}
