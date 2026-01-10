Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer Constants : register(b0)
{
    float Sharpness; // 0.0 to 1.0
    float3 Padding;
}

// AMD FidelityFX CAS-like Implementation
// "Contrast Adaptive Sharpening"
// Uses a negative lobe to validly sharpen the image while preserving coherence.
void RCAS(uint2 pos, float sharpness, uint width, uint height)
{
    if (pos.x >= width || pos.y >= height) return;
    
    // 1. Fetch Center and Neighbors (Cross)
    float4 c = InputTexture[pos];
    float3 n = InputTexture[int2(pos.x, max(0, (int)pos.y - 1))].rgb;
    float3 s = InputTexture[int2(pos.x, min((int)height - 1, (int)pos.y + 1))].rgb;
    float3 w = InputTexture[int2(max(0, (int)pos.x - 1), pos.y)].rgb;
    float3 e = InputTexture[int2(min((int)width - 1, (int)pos.x + 1), pos.y)].rgb;

    // 2. Calculate Lobe Weight
    // Sharpness 0.0 -> Lobe 0.0 (Identity)
    // Sharpness 1.0 -> Lobe -0.2 (Strong Sharpening)
    // The typical CAS limit involves a negative lobe.
    float lobe = lerp(0.0f, -0.2f, sharpness);
    
    // 3. Apply CAS Convolution
    // Formula: (Center + Lobe * SumNeighbors) / (1 + 4 * Lobe)
    float3 neighborSum = n + s + w + e;
    float3 numerator = c.rgb + lobe * neighborSum;
    float denominator = 1.0f + 4.0f * lobe;
    
    // Avoid division by zero (though denominator won't be 0 with lobe > -0.25)
    float3 result = numerator / denominator;
    
    // 4. Output (Saturate to keep valid color range)
    OutputTexture[pos] = float4(saturate(result), c.a);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint w, h;
    InputTexture.GetDimensions(w, h);
    RCAS(dispatchThreadId.xy, Sharpness, w, h);
}
