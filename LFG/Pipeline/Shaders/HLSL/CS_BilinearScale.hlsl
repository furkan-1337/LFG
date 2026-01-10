Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

SamplerState LinearClampSampler : register(s0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint outW, outH;
    Output.GetDimensions(outW, outH);
    
    if (id.x >= outW || id.y >= outH) return;

    // Calculate UV coordinates for the center of the output pixel
    float2 uv = (float2(id.xy) + 0.5f) / float2(outW, outH);
    
    // Sample Input with Linear filtering
    // Note: Compute Shaders usually require 'SamplerState' and 'SampleLevel' (lod 0)
    float4 color = Input.SampleLevel(LinearClampSampler, uv, 0);
    
    Output[id.xy] = color;
}
