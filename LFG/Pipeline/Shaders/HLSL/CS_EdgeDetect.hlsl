Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputEdge : register(u0);

[numthreads(32, 32, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    uint w, h;
    InputTexture.GetDimensions(w, h);

    if (pos.x >= w || pos.y >= h)
        return;

    // Sobel Kernels
    // Gx: -1 0 1   Gy: -1 -2 -1
    //     -2 0 2       0  0  0
    //     -1 0 1       1  2  1

    float3 c00 = InputTexture[pos + int2(-1, -1)].rgb;
    float3 c10 = InputTexture[pos + int2( 0, -1)].rgb;
    float3 c20 = InputTexture[pos + int2( 1, -1)].rgb;
    float3 c01 = InputTexture[pos + int2(-1,  0)].rgb;
    float3 c21 = InputTexture[pos + int2( 1,  0)].rgb;
    float3 c02 = InputTexture[pos + int2(-1,  1)].rgb;
    float3 c12 = InputTexture[pos + int2( 0,  1)].rgb;
    float3 c22 = InputTexture[pos + int2( 1,  1)].rgb;

    // Luminance weights (Rec. 709)
    const float3 lum = float3(0.2126, 0.7152, 0.0722);
    
    // Convert neighborhood to luminance
    float l00 = dot(c00, lum);
    float l10 = dot(c10, lum);
    float l20 = dot(c20, lum);
    float l01 = dot(c01, lum);
    float l21 = dot(c21, lum);
    float l02 = dot(c02, lum);
    float l12 = dot(c12, lum);
    float l22 = dot(c22, lum);

    float Gx = -l00 + l20 - 2.0*l01 + 2.0*l21 - l02 + l22;
    float Gy = -l00 - 2.0*l10 - l20 + l02 + 2.0*l12 + l22;

    float magnitude = sqrt(Gx*Gx + Gy*Gy);
    
    // Store edge magnitude (Clamp to 0-1)
    OutputEdge[pos] = float4(magnitude, magnitude, magnitude, 1.0);
}
