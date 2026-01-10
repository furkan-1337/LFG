Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

cbuffer CBUpscale : register(b0)
{
    int Mode; // 0=Nearest, 1=Bilinear, 2=Bicubic, 3=Lanczos
    int Radius; // For Lanczos (e.g., 2 or 3)
    float2 InputSize; // Width, Height
    float2 Padding;
}

// Helper: Cubic Weight
float CubicWeight(float x)
{
    const float B = 0.0f;
    const float C = 0.5f;
    float ax = abs(x);
    if (ax < 1.0f)
        return ((12 - 9 * B - 6 * C) * ax * ax * ax + (-18 + 12 * B + 6 * C) * ax * ax + (6 - 2 * B)) / 6.0f;
    else if (ax < 2.0f)
        return ((-B - 6 * C) * ax * ax * ax + (6 * B + 30 * C) * ax * ax + (-12 * B - 48 * C) * ax + (8 * B + 24 * C)) / 6.0f;
    else
        return 0.0f;
}

// Helper: Sinc function
float Sinc(float x)
{
    if (x == 0.0f) return 1.0f;
    float pi_x = 3.14159265f * x;
    return sin(pi_x) / pi_x;
}

// Helper: Lanczos Weight
float LanczosWeight(float x, int a)
{
    if (abs(x) >= a) return 0.0f;
    return Sinc(x) * Sinc(x / a);
}

float4 SampleNearest(int2 coord)
{
    return Input[coord]; // Direct texel fetch
}

float4 SampleBilinear(float2 uv)
{
    return Input.SampleLevel(LinearSampler, uv, 0);
}

// Bicubic Sampling (Catmull-Rom)
float4 SampleBicubic(float2 uv)
{
    float2 texSize = InputSize;
    float2 samplePos = uv * texSize - 0.5f;
    int2 tc = floor(samplePos);
    float2 f = samplePos - tc;

    float4 sum = 0;
    float totalWeight = 0;
    
    // 4x4 Neighborhood
    [unroll]
    for (int y = -1; y <= 2; y++)
    {
        [unroll]
        for (int x = -1; x <= 2; x++)
        {
            int2 coord = clamp(tc + int2(x, y), int2(0,0), int2(texSize)-1);
            float w = CubicWeight(x - f.x) * CubicWeight(y - f.y);
            sum += Input[coord] * w;
            totalWeight += w;
        }
    }
    return sum / totalWeight; // Normalization helps if edge clamping issues occur
}

// Lanczos Sampling
float4 SampleLanczos(float2 uv, int a)
{
    float2 texSize = InputSize;
    float2 samplePos = uv * texSize - 0.5f;
    int2 tc = floor(samplePos);
    float2 f = samplePos - tc;

    float4 sum = 0;
    float totalWeight = 0;

    // Window size: -a+1 to a
    for (int y = -a + 1; y <= a; y++)
    {
        for (int x = -a + 1; x <= a; x++)
        {
            int2 coord = clamp(tc + int2(x, y), int2(0,0), int2(texSize)-1);
            float w = LanczosWeight(float(x) - f.x, a) * LanczosWeight(float(y) - f.y, a);
            sum += Input[coord] * w;
            totalWeight += w;
        }
    }
    
    // Prevent ringing artifacts (optional clamp or just raw)
    // For now, raw output, but weight normalization is key.
    return (totalWeight > 0.0001f) ? (sum / totalWeight) : float4(0,0,0,0);
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint outW, outH;
    Output.GetDimensions(outW, outH);
    if (id.x >= outW || id.y >= outH) return;

    float2 uv = (float2(id.xy) + 0.5f) / float2(outW, outH);
    
    float4 color = 0;
    
    // Switch is acceptable here
    // Mode 0 = Native (Should probably not be dispatched, but behaves like Nearest/Copy if hit)
    
    if (Mode == 1) // Nearest
    {
        float2 iUV = uv * InputSize;
        color = Input[int2(iUV)]; // or simple cast
    }
    else if (Mode == 2) // Bilinear
    {
        color = SampleBilinear(uv);
    }
    else if (Mode == 3) // Bicubic
    {
        color = SampleBicubic(uv);
    }
    else if (Mode == 4) // Lanczos
    {
        color = SampleLanczos(uv, Radius);
    }
    else // Fallback (Native or Invalid)
    {
        // For Native, we essentially want 1:1 copy, which Nearest handles correctly if sizes match
        float2 iUV = uv * InputSize;
        color = Input[int2(iUV)]; 
    }

    Output[id.xy] = color;
}
