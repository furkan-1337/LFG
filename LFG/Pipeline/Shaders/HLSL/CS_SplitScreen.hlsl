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
