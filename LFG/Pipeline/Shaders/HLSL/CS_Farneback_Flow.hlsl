Texture2D<float4> PolyCurr : register(t0); // Gx, Gy, Rxx, Ryy
Texture2D<float4> PolyPrev : register(t1);
Texture2D<float2> MotionInput : register(t2);
RWTexture2D<float2> MotionOutput : register(u0); // Stores computed flow

SamplerState LinearSampler : register(s0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    uint w, h;
    PolyCurr.GetDimensions(w, h); // Assume same size
    if (pos.x >= w || pos.y >= h) return;

    // 1. Initial Guess
    float2 d0 = MotionInput[pos];
    // Scale d0 if needed? Assuming pixel units here.
    
    // 2. Accumulate G and h over a window (e.g., 5x5 or 9x9)
    // Minimizing sum(|2 R d + deltaB|^2)
    // Solution: d = - sum(R * deltaB) / sum(2 * R^2)
    
    float sum_Rxx_dBx = 0;
    float sum_Rxx2 = 0;
    float sum_Ryy_dBy = 0;
    float sum_Ryy2 = 0;
    
    // 5x5 Window
    for(int y = -2; y <= 2; ++y) {
        for(int x = -2; x <= 2; ++x) {
            int2 p = pos + int2(x, y);
            if(p.x < 0 || p.y < 0 || p.x >= (int)w || p.y >= (int)h) continue;
            
            // Sample Curr
            float4 c = PolyCurr[p];
            
            // Sample Prev at shifted location
            float2 p_shifted = float2(p) + d0;
            float4 p_prev = PolyPrev.SampleLevel(LinearSampler, p_shifted / float2(w, h), 0);
            
            // Coeffs
            float rxx = (c.z + p_prev.z) * 0.5;
            float ryy = (c.w + p_prev.w) * 0.5;
            float dbx = c.x - p_prev.x;
            float dby = c.y - p_prev.y;
            
            // Assuming Diagonal A implies independent x/y optimization (Simplification)
            // Weighting: Uniform for now.
            sum_Rxx_dBx += rxx * dbx;
            sum_Rxx2 += rxx * rxx;
            
            sum_Ryy_dBy += ryy * dby;
            sum_Ryy2 += ryy * ryy;
        }
    }
    
    float2 delta = 0;
    float eps = 0.0001; // Avoid divide by zero
    delta.x = -sum_Rxx_dBx / (2.0 * sum_Rxx2 + eps);
    delta.y = -sum_Ryy_dBy / (2.0 * sum_Ryy2 + eps);
    
    // Clamp Refinement (Stability)
    delta = clamp(delta, -2.0, 2.0); // Don't jump too far in one refinement step
    
    MotionOutput[pos] = d0 + delta;
}
