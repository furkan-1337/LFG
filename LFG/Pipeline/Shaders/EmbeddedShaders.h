#pragma once

namespace EmbeddedShaders
{
    inline const char* CS_AdaptiveVariance = R"(
Texture2D<float4> Input : register(t0);
RWTexture2D<float> OutputVariance : register(u0); // Stores variance 0.0-1.0

// Block Size for assessment (e.g. 16x16)
#define BLOCK_SIZE 16

[numthreads(16, 16, 1)]
void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    // Each thread group processes one 16x16 block of the image
    // But we want to output ONE value per block? 
    // Usually easier to run one thread per output pixel, gathering source pixels.
    // If we do 1:1 map, it's slow.
    // Let's assume OutputVariance is 1/16th size of Input.
    
    // Dispatch is (InputWidth/16, InputHeight/16, 1)
    // Flattened Thread ID doesn't help if we only launch 1 thread per group?
    // Let's use [numthreads(8,8,1)] acting on a region.
    
    // SIMPLIFIED APPROACH:
    // This shader runs per PIXEL of the Output Grid (Low Res).
    // Each thread samples the corresponding 16x16 block in Input.
}

// Redefine to run per Low-Res Pixel
[numthreads(8, 8, 1)]
void CalculateVariance(uint3 id : SV_DispatchThreadID)
{
    uint gridW, gridH;
    OutputVariance.GetDimensions(gridW, gridH);
    if (id.x >= gridW || id.y >= gridH) return;

    uint2 basePos = id.xy * BLOCK_SIZE;
    
    float sum = 0;
    float sumSq = 0;
    
    // Sample 16x16 block
    // Unrolling loops for performance if possible, but 256 samples is a lot.
    // Use strided sampling for speed (e.g. step 2 or 4)
    for (uint y = 0; y < BLOCK_SIZE; y+=2)
    {
        for (uint x = 0; x < BLOCK_SIZE; x+=2)
        {
            float3 col = Input[uint2(basePos.x + x, basePos.y + y)].rgb;
            float lum = dot(col, float3(0.299, 0.587, 0.114));
            sum += lum;
            sumSq += lum * lum;
        }
    }
    
    // Count = (16/2) * (16/2) = 64 samples
    float count = 64.0f;
    float mean = sum / count;
    float variance = (sumSq / count) - (mean * mean);
    
    // Normalize (Variance can be small, so scale it up)
    // Heuristic: Variance > 0.01 is "complex"
    float normalizedVar = saturate(variance * 100.0f);
    
    OutputVariance[id.xy] = normalizedVar;
}
)";

    inline const char* CS_BidirectionalConsistency = R"(
Texture2D<float2> FwdFlow : register(t0);
Texture2D<float2> BwdFlow : register(t1);

RWTexture2D<float2> OutputFlow : register(u0);
RWTexture2D<float> OutputConfidence : register(u1); // Optional confidence map

cbuffer CB : register(b0)
{
	float Tolerance; // e.g. 1.0 - 5.0 pixels
	float3 Padding;
};

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	OutputFlow.GetDimensions(width, height);
	if (id.x >= width || id.y >= height) return;

	float2 fwd = FwdFlow[id.xy];
	
	// Backward flow is at the position pointed to by forward flow
	int2 targetPos = int2(id.x + fwd.x, id.y + fwd.y);
	
	// Boundary check
	float confidence = 1.0f;
	if (targetPos.x < 0 || targetPos.y < 0 || targetPos.x >= (int)width || targetPos.y >= (int)height)
	{
		confidence = 0.0f;
	}
	else
	{
		float2 bwd = BwdFlow[targetPos];
		
		// Ideally: Fwd + Bwd approx 0.
		// Delta = |Fwd + Bwd|
		float2 diff = abs(fwd + bwd);
		float dist = length(diff);
		
		if (dist > Tolerance)
		{
			// Occlusion or Bad Match
			confidence = max(0.0f, 1.0f - (dist - Tolerance) * 0.5f);
		}
	}
	
	// Write sanitized flow (maybe damped by confidence, or just passed through)
	// For now, we pass through Forward Flow, but write Confidence for the Interpolator to use.
	OutputFlow[id.xy] = fwd;
	OutputConfidence[id.xy] = confidence;
}
)";

    inline const char* CS_BilinearScale = R"(
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
)";

    inline const char* CS_BlockMatching = R"(
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
)";

    inline const char* CS_DIS_Flow = R"(
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
)";

    inline const char* CS_DebugView = R"(
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
)";

    inline const char* CS_Downsample = R"(
Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Simple 2x2 box filter downsampling
    uint2 srcPos = dispatchThreadId.xy * 2;
    
    float4 c0 = Input[srcPos + uint2(0, 0)];
    float4 c1 = Input[srcPos + uint2(1, 0)];
    float4 c2 = Input[srcPos + uint2(0, 1)];
    float4 c3 = Input[srcPos + uint2(1, 1)];
    
    float4 average = (c0 + c1 + c2 + c3) * 0.25f;
    
    Output[dispatchThreadId.xy] = average;
}
)";

    inline const char* CS_EdgeDetect = R"(
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
)";

    inline const char* CS_Farneback_Expansion = R"(
Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 pos = int2(dispatchThreadId.xy);
    uint w, h;
    Input.GetDimensions(w, h);
    if (pos.x >= w || pos.y >= h) return;

    // Use a 5x5 neighborhood to compute:
    // b_x (Gradient X)
    // b_y (Gradient Y)
    // r_xx (Curvature X)
    // r_yy (Curvature Y)
    
    // Explicit 5x5 load
    float val[5][5];
    [unroll]
    for(int y = -2; y <= 2; ++y) {
        [unroll]
        for(int x = -2; x <= 2; ++x) {
            val[y+2][x+2] = Input[pos + int2(x,y)].r;
        }
    }

    // Gradient X (Smoothed Sobel-like)
    // -1 0 1
    // -2 0 2
    // -1 0 1
    float gx = 0;
    gx += -1*val[1][0] + 1*val[1][4]; 
    gx += -2*val[2][0] + 2*val[2][4];
    gx += -1*val[3][0] + 1*val[3][4];
    gx /= 8.0;

    // Gradient Y 
    float gy = 0;
    gy += -1*val[0][1] + 1*val[4][1];
    gy += -2*val[0][2] + 2*val[4][2];
    gy += -1*val[0][3] + 1*val[4][3];
    gy /= 8.0;

    // Second Derivatives (Curvature)
    // r_xx ~ (1 -2 1)
    float rxx = (val[2][0] - 2.0*val[2][2] + val[2][4]) * 0.25; // Scale
    
    // r_yy ~ (1 -2 1)T
    float ryy = (val[0][2] - 2.0*val[2][2] + val[4][2]) * 0.25;

    // Output: GradientX, GradientY, CurvatureX, CurvatureY
    Output[pos] = float4(gx, gy, rxx, ryy);
}
)";

    inline const char* CS_Farneback_Flow = R"(
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
)";

    inline const char* CS_HUDMask = R"(
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
)";

    inline const char* CS_Interpolate = R"(
Texture2D<float4> TexCurrent : register(t0);
Texture2D<float4> TexPrev : register(t1);
Texture2D<float2> TexMotion : register(t2);
Texture2D<float> TexMask : register(t3);
StructuredBuffer<uint> GlobalStats : register(t4); // [Scene Change Stats]

RWTexture2D<float4> OutputFrame : register(u0);

SamplerState LinearSampler : register(s0);

cbuffer Settings : register(b0)
{
    float Factor; // Interpolation factor (0.0 - 1.0)
    int SceneChangeThreshold; // If GlobalStats[0] > Threshold, SKIP
    float GhostingStrength;
    float Padding;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadId.xy;
    
    // [Scene Change Safety]
    if (GlobalStats[0] > (uint)SceneChangeThreshold)
    {
        OutputFrame[pos] = TexCurrent[pos];
        return;
    }
    
    float mask = TexMask[pos];
    if (mask > 0.5f)
    {
        OutputFrame[pos] = TexCurrent[pos];
        return;
    }
    
    // Fetch Motion Vector (in pixels)
    float2 motion = TexMotion[pos];
    
    uint w, h;
    TexCurrent.GetDimensions(w, h);
    float2 texSize = float2(w, h);
    float2 uv = (float2(pos) + 0.5f) / texSize;
    float2 motionUV = motion / texSize;
    
    // Interpolate
    float4 pixelPrev = TexPrev.SampleLevel(LinearSampler, uv + motionUV * Factor, 0);
    float4 pixelCurr = TexCurrent.SampleLevel(LinearSampler, uv - motionUV * (1.0f - Factor), 0);
    // Wait, original was (1.0f - Factor), 0). I will replicate exact text. 
    // Actually, I should just paste exact content.
    // Re-checking previous read...
    // "float4 pixelCurr = TexCurrent.SampleLevel(LinearSampler, uv - motionUV * (1.0f - Factor), 0);"
    // Okay.
    
    // Re-writing with exact content logic:
    pixelCurr = TexCurrent.SampleLevel(LinearSampler, uv - motionUV * (1.0f - Factor), 0);
    
    float4 result = lerp(pixelPrev, pixelCurr, Factor);

    // [Ghosting Reduction]
    // Clamp result to the neighborhood of the Current frame at the target location.
    if (GhostingStrength > 0.0f)
    {
        // 5-tap neighborhood (Center + Plus)
        float4 c = TexCurrent.SampleLevel(LinearSampler, uv, 0);
        float4 n = TexCurrent.SampleLevel(LinearSampler, uv + float2(0, 1) / texSize, 0);
        float4 s = TexCurrent.SampleLevel(LinearSampler, uv - float2(0, 1) / texSize, 0);
        float4 e = TexCurrent.SampleLevel(LinearSampler, uv + float2(1, 0) / texSize, 0);
        float4 w = TexCurrent.SampleLevel(LinearSampler, uv - float2(1, 0) / texSize, 0);
        
        float4 minColor = min(c, min(n, min(s, min(e, w))));
        float4 maxColor = max(c, max(n, max(s, max(e, w))));
        
        float4 clamped = clamp(result, minColor, maxColor);
        result = lerp(result, clamped, GhostingStrength);
    }

    OutputFrame[pos] = result;
}
)";
    // Note: I manually corrected the syntax in CS_Interpolate string for safety (removed extra closing paren if any, but string literal just copies chars).
    // The previous read showed: `pixelCurr = TexCurrent.SampleLevel(LinearSampler, uv - motionUV * (1.0f - Factor), 0);` 
    // Which is correct.

    inline const char* CS_MotionSmooth = R"(
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
)";

    inline const char* CS_RCAS = R"(
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer Constants : register(b0)
{
    float Sharpness; // 0.0 to 1.0
    float3 Padding;
}

// AMD FidelityFX CAS-like Implementation
// "Contrast Adaptive Sharpening"
// Uses a negative lobe to validly sharpen the image while preserving coherence.
void RCAS(uint2 pos, float sharpness, uint width, uint height)
{
    if (pos.x >= width || pos.y >= height) return;
    
    // 1. Fetch Center and Neighbors (Cross)
    float4 c = InputTexture[pos];
    float3 n = InputTexture[int2(pos.x, max(0, (int)pos.y - 1))].rgb;
    float3 s = InputTexture[int2(pos.x, min((int)height - 1, (int)pos.y + 1))].rgb;
    float3 w = InputTexture[int2(max(0, (int)pos.x - 1), pos.y)].rgb;
    float3 e = InputTexture[int2(min((int)width - 1, (int)pos.x + 1), pos.y)].rgb;

    // 2. Calculate Lobe Weight
    // Sharpness 0.0 -> Lobe 0.0 (Identity)
    // Sharpness 1.0 -> Lobe -0.2 (Strong Sharpening)
    // The typical CAS limit involves a negative lobe.
    float lobe = lerp(0.0f, -0.2f, sharpness);
    
    // 3. Apply CAS Convolution
    // Formula: (Center + Lobe * SumNeighbors) / (1 + 4 * Lobe)
    float3 neighborSum = n + s + w + e;
    float3 numerator = c.rgb + lobe * neighborSum;
    float denominator = 1.0f + 4.0f * lobe;
    float3 result = numerator / denominator;
    
    // 4. Output (Saturate to keep valid color range)
    OutputTexture[pos] = float4(saturate(result), c.a);
    // Correction: 'result' is defined after numerator/denominator calc? 
    // Wait, the read file had:
    // float3 result = numerator / denominator;
    // Let me check my memory of the read...
    // Yes, line 37: float3 result = numerator / denominator;
    // I missed it in the string literal above. Adding it back.
}
// CORRECTION INLINE:
// float3 result = numerator / denominator;
// OutputTexture[pos] = float4(saturate(result), c.a);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint w, h;
    InputTexture.GetDimensions(w, h);
    RCAS(dispatchThreadId.xy, Sharpness, w, h);
}
)";

    // Manually fixing the RCAS string since I can't edit the literal in thought process mid-stream easily.
    // I'll rewrite the RCAS part correctly in the tool call.

    inline const char* CS_SplitScreen = R"(
Texture2D<float4> TexGen : register(t0);
Texture2D<float4> TexReal : register(t1);

RWTexture2D<float4> Output : register(u0);

cbuffer CB : register(b0)
{
	float SplitPos; // 0.0 - 1.0 (Normalized screen width)
	float3 Padding;
};

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint width, height;
	Output.GetDimensions(width, height);

	if (id.x >= width || id.y >= height) return;

	float u = (float)id.x / (float)width;
	float4 color;

	// Use Integer logic for stability
	int splitX = (int)(SplitPos * (float)width);
	int px = (int)id.x;

	// Draw 2px Line
	if (abs(px - splitX) <= 1)
	{
		// Draw White Line
		color = float4(1.0, 1.0, 1.0, 1.0);
	}
	else if (u < SplitPos)
	{
		// Left: Generated Frame (Frame Gen ON)
		color = TexGen[id.xy];
	}
	else
	{
		// Right: Real Frame (Frame Gen OFF - simulating simple repeat)
		color = TexReal[id.xy];
	}

	Output[id.xy] = color;
}
)";

    inline const char* CS_Upsample = R"(
Texture2D<float2> InputMotion : register(t0);
RWTexture2D<float2> OutputMotion : register(u0);

SamplerState LinearSampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Target resolution (upsampled) coordinates
    uint2 dstPos = dispatchThreadId.xy;
    
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
)";

    inline const char* CS_Upscale = R"(
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
)";

} // namespace EmbeddedShaders
