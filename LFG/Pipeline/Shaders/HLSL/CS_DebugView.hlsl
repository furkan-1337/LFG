Texture2D<float2> TexMotion : register(t0);
Texture2D<float> TexMask : register(t1);
RWTexture2D<float4> Output : register(u0);

cbuffer Settings : register(b0)
{
    int Mode; // 1 = Motion, 2 = Mask
    float Scale; // For motion visualization
    int2 Padding;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    
    float4 color = float4(0, 0, 0, 1);
    
    if (Mode == 1) // Motion Vectors
    {
        float2 motion = TexMotion[pos];
        // map x/y directly to r/g. Scale up to see small movements.
        // abs() to see negative motion as color too.
        color.rgb = float3(abs(motion.x), abs(motion.y), 0) * Scale; 
        // Add a small blue component to show static areas
        if (length(motion) == 0) color.b = 0.2f;
    }
    else if (Mode == 2) // HUD Mask
    {
        float mask = TexMask[pos];
        // Mask is 1.0 for HUD (Static), 0.0 for World (Dynamic)
        // Let's make HUD Red and World Grayscale/Dark
        color.rgb = lerp(float3(0.1, 0.1, 0.1), float3(1.0, 0.0, 0.0), mask);
    }
    
    Output[pos] = color;
}
