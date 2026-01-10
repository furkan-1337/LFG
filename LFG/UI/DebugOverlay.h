#pragma once

namespace UI
{
	class DebugOverlay
	{
	public:
		static void Render();
		static void OnPresent(int count = 1);
		static void SetInputLatency(float ms);
		static float GetDisplayFPS();
	};
}
