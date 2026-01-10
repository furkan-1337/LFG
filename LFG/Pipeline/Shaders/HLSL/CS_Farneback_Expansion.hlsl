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
