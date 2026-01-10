Texture2D<float4> TexCurrent : register(t0);
Texture2D<float4> TexPrev : register(t1);
Texture2D<float4> GradsPrev : register(t2); // Gradient of Prev Frame (from Expansion shader)
Texture2D<float2> MotionInput : register(t3);
RWTexture2D<float2> MotionOutput : register(u0);

SamplerState LinearSampler : register(s0);

// DIS (Dense Inverse Search) Approximation
// "Inverse Compositional" logic:
// We align the Current patch to the Previous patch using gradients of Prev.
// This allows pre-computation of gradients.

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    uint w, h;
    TexCurrent.GetDimensions(w, h);
    if (pos.x >= w || pos.y >= h) return;

    // 1. Initial Guess
    float2 d = MotionInput[pos];
    
    // 2. Iterative Refinement (Gradient Descent)
    // 8x8 Patch
    const int RADIUS = 4;
    
    // Iterations (Fast convergence for small tweaks)
    [unroll]
    for(int iter = 0; iter < 4; ++iter)
    {
        float sum_IdIxx = 0; // Sum (ImagesDiff * I_x)
        float sum_IdIyy = 0; // Sum (ImagesDiff * I_y)
        float sum_Ixx2 = 0;  // Sum (I_x^2)
        float sum_Iyy2 = 0;  // Sum (I_y^2)
        
        for(int y = -RADIUS; y < RADIUS; ++y) {
            for(int x = -RADIUS; x < RADIUS; ++x) {
                int2 p = pos + int2(x,y);
                if(p.x < 0 || p.y < 0) continue; 
                
                // I_cur(x)
                float I_curr = TexCurrent[p].r;
                
                // I_prev(x + d)
                float2 uv = (float2(p) + d) / float2(w, h);
                float I_prev = TexPrev.SampleLevel(LinearSampler, uv, 0).r;
                
                // Gradients of Prev(x + d)
                // Note: Standard IC uses Grads of Template (Cur) but for tracking usually we align Cur to Prev
                // Let's use Gradients of Previous interpolated.
                float4 g = GradsPrev.SampleLevel(LinearSampler, uv, 0);
                float Ix = g.x; // Gradient X
                float Iy = g.y; // Gradient Y
                
                float diff = I_curr - I_prev; // Error
                
                sum_IdIxx += diff * Ix;
                sum_IdIyy += diff * Iy;
                sum_Ixx2 += Ix * Ix;
                sum_Iyy2 += Iy * Iy;
            }
        }
        
        // Solve JT J d = JT e
        // Making diagonal assumption for speed
        float eps = 0.001;
        float2 delta;
        delta.x = sum_IdIxx / (sum_Ixx2 + eps);
        delta.y = sum_IdIyy / (sum_Iyy2 + eps);
        
        // Update
        d += delta;
        
        // Early transform break?
        if (dot(delta, delta) < 0.001) break;
    }
    
    MotionOutput[pos] = d;
}
