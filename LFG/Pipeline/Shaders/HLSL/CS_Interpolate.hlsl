Texture2D<float4> TexCurrent : register(t0);
Texture2D<float4> TexPrev : register(t1);
Texture2D<float2> TexMotion : register(t2);
Texture2D<float> TexMask : register(t3);
StructuredBuffer<uint> GlobalStats : register(t4); // [Scene Change Stats]

RWTexture2D<float4> OutputFrame : register(u0);

SamplerState LinearSampler : register(s0);

cbuffer Settings : register(b0)
{
    float Factor; // Interpolation factor (0.0 - 1.0)
    int SceneChangeThreshold; // If GlobalStats[0] > Threshold, SKIP
    float GhostingStrength;
    float Padding;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadId.xy;
    
    // [Scene Change Safety]
    if (GlobalStats[0] > (uint)SceneChangeThreshold)
    {
        OutputFrame[pos] = TexCurrent[pos];
        return;
    }
    
    float mask = TexMask[pos];
    if (mask > 0.5f)
    {
        OutputFrame[pos] = TexCurrent[pos];
        return;
    }
    
    // Fetch Motion Vector (in pixels)
    float2 motion = TexMotion[pos];
    
    uint w, h;
    TexCurrent.GetDimensions(w, h);
    float2 texSize = float2(w, h);
    float2 uv = (float2(pos) + 0.5f) / texSize;
    float2 motionUV = motion / texSize;
    
    // Interpolate
    float4 pixelPrev = TexPrev.SampleLevel(LinearSampler, uv + motionUV * Factor, 0);
    float4 pixelCurr = TexCurrent.SampleLevel(LinearSampler, uv - motionUV * (1.0f - Factor), 0);
    
    float4 result = lerp(pixelPrev, pixelCurr, Factor);

    // [Ghosting Reduction]
    // Clamp result to the neighborhood of the Current frame at the target location.
    if (GhostingStrength > 0.0f)
    {
        // 5-tap neighborhood (Center + Plus)
        float4 c = TexCurrent.SampleLevel(LinearSampler, uv, 0);
        float4 n = TexCurrent.SampleLevel(LinearSampler, uv + float2(0, 1) / texSize, 0);
        float4 s = TexCurrent.SampleLevel(LinearSampler, uv - float2(0, 1) / texSize, 0);
        float4 e = TexCurrent.SampleLevel(LinearSampler, uv + float2(1, 0) / texSize, 0);
        float4 w = TexCurrent.SampleLevel(LinearSampler, uv - float2(1, 0) / texSize, 0);
        
        float4 minColor = min(c, min(n, min(s, min(e, w))));
        float4 maxColor = max(c, max(n, max(s, max(e, w))));
        
        float4 clamped = clamp(result, minColor, maxColor);
        result = lerp(result, clamped, GhostingStrength);
    }

    OutputFrame[pos] = result;
}
