Texture2D<float2> InputMotion : register(t0);
RWTexture2D<float2> OutputMotion : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Target resolution (upsampled) coordinates
    uint2 dstPos = dispatchThreadId.xy;
    
    // We map the target integer coordinate to UV [0,1]
    // But since we are in a Compute Shader, we might just sample using integer indices if we want nearest, 
    // or we need normalized UVs for bilinear.
    
    // Simple Nearest Neighbor Upscale for now (or basic bilinear logic manually):
    // Source index is roughly half of destination
    uint2 srcPos = dstPos / 2;
    
    // Read the coarse motion vector
    float2 coarseVector = InputMotion[srcPos];
    
    // Scale the vector by 2.0 because the image domain is 2x larger
    // Motion of 1 pixel in coarse level corresponds to 2 pixels in fine level (usually)
    // IMPORTANT: Check coordinate system. If normalized, no scale needed? 
    // Usually motion vectors are in pixels. So we multiply by 2.
    float2 fineVector = coarseVector * 2.0f;
    
    OutputMotion[dstPos] = fineVector;
}
