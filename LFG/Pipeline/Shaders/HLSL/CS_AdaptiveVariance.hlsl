Texture2D<float4> Input : register(t0);
RWTexture2D<float> OutputVariance : register(u0); // Stores variance 0.0-1.0

// Block Size for assessment (e.g. 16x16)
#define BLOCK_SIZE 16

[numthreads(16, 16, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    // Each thread group processes one 16x16 block of the image
    // But we want to output ONE value per block? 
    // Usually easier to run one thread per output pixel, gathering source pixels.
    // If we do 1:1 map, it's slow.
    // Let's assume OutputVariance is 1/16th size of Input.
    
    // Dispatch is (InputWidth/16, InputHeight/16, 1)
    // Flattened Thread ID doesn't help if we only launch 1 thread per group?
    // Let's use [numthreads(8,8,1)] acting on a region.
    
    // SIMPLIFIED APPROACH:
    // This shader runs per PIXEL of the Output Grid (Low Res).
    // Each thread samples the corresponding 16x16 block in Input.
}

// Redefine to run per Low-Res Pixel
[numthreads(8, 8, 1)]
void CalculateVariance(uint3 id : SV_DispatchThreadID)
{
    uint gridW, gridH;
    OutputVariance.GetDimensions(gridW, gridH);
    if (id.x >= gridW || id.y >= gridH) return;

    uint2 basePos = id.xy * BLOCK_SIZE;
    
    float sum = 0;
    float sumSq = 0;
    
    // Sample 16x16 block
    // Unrolling loops for performance if possible, but 256 samples is a lot.
    // Use strided sampling for speed (e.g. step 2 or 4)
    for (uint y = 0; y < BLOCK_SIZE; y+=2)
    {
        for (uint x = 0; x < BLOCK_SIZE; x+=2)
        {
            float3 col = Input[uint2(basePos.x + x, basePos.y + y)].rgb;
            float lum = dot(col, float3(0.299, 0.587, 0.114));
            sum += lum;
            sumSq += lum * lum;
        }
    }
    
    // Count = (16/2) * (16/2) = 64 samples
    float count = 64.0f;
    float mean = sum / count;
    float variance = (sumSq / count) - (mean * mean);
    
    // Normalize (Variance can be small, so scale it up)
    // Heuristic: Variance > 0.01 is "complex"
    float normalizedVar = saturate(variance * 100.0f);
    
    OutputVariance[id.xy] = normalizedVar;
}
