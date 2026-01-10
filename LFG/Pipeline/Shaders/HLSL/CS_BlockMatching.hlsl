Texture2D<float4> TexCurrent : register(t0);
Texture2D<float4> TexPrev : register(t1);
Texture2D<float2> InputInitMotion : register(t2); // NEW: Initial guess from lower level
RWTexture2D<float2> OutputMotion : register(u0);
RWStructuredBuffer<uint> GlobalStats : register(u1); // [Counter]

SamplerState LinearSampler : register(s0);

// Parameters
cbuffer CB : register(b0)
{
    int Width;
    int Height;
    int BlockSize; 
    int SearchRadius; 
    int EnableSubPixel;
    int UseInitMotion; // NEW: 0 or 1
    int2 Padding;      // Alignment
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Global pixel position
    int2 pos = int2(dispatchThreadId.xy);
    
    if (pos.x >= Width || pos.y >= Height)
        return;

    // Center pixel of the block
    float4 targetPixel = TexCurrent[pos];
    
    // Initial Guess
    int2 searchCenter = int2(0, 0);
    if (UseInitMotion)
    {
        // Read from lower level (already upsampled or same size)
        float2 initVec = InputInitMotion[pos];
        searchCenter = int2(round(initVec.x), round(initVec.y));
    }

    // [Ultra Optimization] Fast Path for Static/Perfect Guess
    // Check the exact guess position first.
    {
         int2 searchPos = pos + searchCenter;
         if (searchPos.x >= 0 && searchPos.y >= 0 && searchPos.x < Width && searchPos.y < Height)
         {
             float4 candidatePixel = TexPrev[searchPos];
             float3 diff = abs(targetPixel.rgb - candidatePixel.rgb);
             float sad = diff.r + diff.g + diff.b;
             
             if (sad < 0.001f) // Virtually identical
             {
                 OutputMotion[pos] = float2(searchCenter.x, searchCenter.y);
                 return; // <--- EARLY EXIT: Skip entire loop
             }
         }
    }

    float minSAD = 999999.0f;
    int2 bestVector = searchCenter; // Default to initial guess

    // Refined Search around Center (Reduced radius is adequate if guess is good)
    // If not using init motion, we search full radius.
    // If using init motion, we could technically search a smaller radius, but for safety lets keep it.
    
    [loop] // Use loop for variable radius, unroll is only for constant
    for (int y = -SearchRadius; y <= SearchRadius; ++y)
    {
        for (int x = -SearchRadius; x <= SearchRadius; ++x)
        {
            int2 offset = int2(x, y);
            int2 searchPos = pos + searchCenter + offset;
            
            // Boundary check
            if (searchPos.x < 0 || searchPos.y < 0 || searchPos.x >= Width || searchPos.y >= Height)
                continue;

            float4 candidatePixel = TexPrev[searchPos];
            
            float3 diff = abs(targetPixel.rgb - candidatePixel.rgb);
            float sad = diff.r + diff.g + diff.b;

            if (sad < minSAD)
            {
                minSAD = sad;
                bestVector = searchCenter + offset;
                
                // Early Exit: Perfect match found (SAD ~ 0)
                // This speeds up static areas massively (HUD, Skyboxes)
                if (sad < 0.001f) 
                {
                     // Break outer loop manually
                     y = SearchRadius + 1; 
                     break;
                }
            }
        }
    }

    // Write integer result first
    float2 finalVector = float2(bestVector.x, bestVector.y);
    
    // [Scene Change Detection]
    // If the best match is still terrible, it means we found NOTHING similar.
    // If many blocks fail, it's a scene change.
    // Normalized threshold: 0.15 (15% avg diff per pixel)
    // Scale: Sad is sum of 3 channels * BlockSize^2
    float avgDiff = minSAD / (max(1, BlockSize * BlockSize) * 3.0f);
    if (avgDiff > 0.15f)
    {
        InterlockedAdd(GlobalStats[0], 1);
    }
    


    // Sub-Pixel Refinement (Bilinear Check)
    if (EnableSubPixel > 0)
    {
        float2 bestSub = finalVector;
        float minSubSAD = minSAD;
        
        float2 offsets[4] = { float2(0.5, 0), float2(-0.5, 0), float2(0, 0.5), float2(0, -0.5) };
        
        uint w, h;
        TexPrev.GetDimensions(w, h);
        float2 texSize = float2(w, h);
        
        for(int i=0; i<4; ++i)
        {
             // Candidate Pos in Prev: Original Pos + BestIntegerVec + Offset
             float2 checkPos = float2(pos) + finalVector + offsets[i];
             float2 uv = (checkPos + 0.5f) / texSize;
             
             float4 candidatePixel = TexPrev.SampleLevel(LinearSampler, uv, 0);
             
             float3 diff = abs(targetPixel.rgb - candidatePixel.rgb);
             float sad = diff.r + diff.g + diff.b;
             
             if (sad < minSubSAD)
             {
                 minSubSAD = sad;
                 bestSub = finalVector + offsets[i];
             }
        }
        finalVector = bestSub;
    }
    
    OutputMotion[pos] = finalVector;
}
