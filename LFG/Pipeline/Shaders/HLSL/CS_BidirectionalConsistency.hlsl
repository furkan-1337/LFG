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
