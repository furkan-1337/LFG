Texture2D<float2> InputMotion : register(t0);
RWTexture2D<float2> OutputMotion : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    
    // Simple 3x3 Box Blur for Motion Smoothing
    // (5x5 might be too heavy for this stage, let's start with 3x3)
    float2 sum = float2(0, 0);
    float weight = 0.0f;
    
    // We strictly clamp to borders? Or just check bounds.
    // HLSL Loads return 0 out of bounds usually (depending on state), but safe to check.
    
    uint w, h;
    InputMotion.GetDimensions(w, h);
    if (pos.x >= w || pos.y >= h) return;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 offsetPos = pos + int2(x, y);
             if (offsetPos.x >= 0 && offsetPos.y >= 0 && offsetPos.x < w && offsetPos.y < h)
             {
                 sum += InputMotion[offsetPos];
                 weight += 1.0f;
             }
        }
    }
    
    OutputMotion[pos] = sum / weight;
}
