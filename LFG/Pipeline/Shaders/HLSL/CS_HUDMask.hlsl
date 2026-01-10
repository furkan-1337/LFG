Texture2D<float4> TexCurrent : register(t0);
Texture2D<float4> TexPrev : register(t1);
Texture2D<float4> EdgeTexture : register(t2); // [Edge Detect]
RWTexture2D<float> OutputMask : register(u0);

cbuffer Settings : register(b0)
{
    float Threshold;
    int UseEdgeDetect; // [Edge Detect]
    float2 Padding;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    
    // Boundary check handled by resource size usually, but good practice
    float4 curr = TexCurrent[pos];
    float4 prev = TexPrev[pos];
    
    // Calculate difference
    float4 diff = abs(curr - prev);
    float val = diff.r + diff.g + diff.b;
    
    // Base Static Check
    float isStatic = (val < Threshold) ? 1.0f : 0.0f;
    float isHUD = isStatic;

    // [Edge Detection Logic]
    // UI elements usually have sharp edges AND are static.
    // If Edge Protection is enabled:
    // We only consider it HUD if it is Static AND has high Edge Magnitude.
    // This prevents flat textures (sky, walls) from being falsely flagged as HUD just because they are static.
    if (UseEdgeDetect > 0)
    {
        float edgeMag = EdgeTexture[pos].r; // Read magnitude from Sobel pass
        
        // If edge magnitude is low, it's likely a flat surface, not UI text/border.
        // We require SIGNIFICANT edge presence to confirm it's a HUD element.
        // This makes the mask more conservative (safer).
        if (edgeMag < 0.1f) 
        {
            isHUD = 0.0f; 
        }
    }
    
    // Simple overwrite for now. 
    // In advanced versions, we would use a decay buffer to keep the mask stable.
    OutputMask[pos] = isHUD;
}
