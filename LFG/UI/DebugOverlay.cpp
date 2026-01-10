#include "DebugOverlay.h"
#include <Dependencies/ImGui/imgui.h>
#include "../Pipeline/Generation/FrameGeneration.h"

// Helper for Frame Time History
static float frameTimes[120] = { 0 };
static int frameTimeOffset = 0;

// Statics for FPS Counting
// Statics for FPS Counting
static int PresentedFrames = 0;
static float DisplayFPS = 0.0f;
static float LastMeasureTime = 0.0f;
static float CurrentInputLatency = 0.0f; // New

void UI::DebugOverlay::SetInputLatency(float ms)
{
	CurrentInputLatency = ms;
}

void UI::DebugOverlay::OnPresent(int count)
{
	PresentedFrames += count;
	
	// Update FPS every 1000ms
	float currentTime = ImGui::GetTime();
	if (currentTime - LastMeasureTime >= 1.0f)
	{
		DisplayFPS = (float)PresentedFrames / (currentTime - LastMeasureTime);
		PresentedFrames = 0;
		LastMeasureTime = currentTime;
	}
}

float UI::DebugOverlay::GetDisplayFPS()
{
	return DisplayFPS;
}

void UI::DebugOverlay::Render()
{
	auto& settings = FrameGeneration::Instance().GetSettings();
	if (!settings.ShowDebugOverlay) return;

	// Style
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.90f)); // Deep dark bg

	ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);

	if (ImGui::Begin("LFG Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		float realFPS = ImGui::GetIO().Framerate;
		float frameTime = 1000.0f / (realFPS > 0 ? realFPS : 1.0f);

		// History Update
		frameTimes[frameTimeOffset] = frameTime;
		frameTimeOffset = (frameTimeOffset + 1) % 120;

        // Custom Header Line
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - 15, p.y - 15), ImVec2(p.x + ImGui::GetWindowWidth() - 15, p.y - 12), IM_COL32(0, 200, 255, 255)); // Cyan accent top strip
        
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Lufzy's Frame Generation");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "  - furkan.1337");
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 0.8f), "github.com/furkan-1337");
		
        ImGui::Dummy(ImVec2(0, 10));

        // ----------------- HERO FPS -----------------
        if (FrameGeneration::Instance().IsEnabled())
		{
			float outFPS = DisplayFPS;
			int multiplier = settings.MultiFrameCount + 1;
			if (outFPS < 1.0f) outFPS = realFPS * multiplier; 

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Ensuring default font, but scaled would be nice if available
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.6f), "DISPLAY FPS");
            ImGui::SetWindowFontScale(2.0f);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%.0f", outFPS);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "^ %dx", multiplier);
            
            // Sub-stat
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Native: %.0f FPS", realFPS);
            ImGui::PopFont();
		}
		else
		{
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.6f), "NATIVE FPS");
            ImGui::SetWindowFontScale(2.0f);
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.0f", realFPS);
            ImGui::SetWindowFontScale(1.0f);
		}

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 5));

        // ----------------- METRICS TABLE -----------------
        if (ImGui::BeginTable("StatsTable", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            // Frametime
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Frame Time");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f ms", frameTime);

            // Latency
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Input Latency");
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 0.4f, 1, 1), "%.1f ms", CurrentInputLatency);

            if (FrameGeneration::Instance().IsEnabled())
            {
                // Gen Time
                float genTime = FrameGeneration::Instance().GetLastGenerationTime();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Gen Time");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%.2f ms", genTime);

                // Mode
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Preset");
                ImGui::TableSetColumnIndex(1); 
    			if (settings.MaxPyramidLevel == 4) ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Maximum");
    			else if (settings.MaxPyramidLevel == 3) ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "Extreme");
    			else if (settings.MaxPyramidLevel == 2) ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Ultra");
    			else if (settings.MaxPyramidLevel == 1) ImGui::Text("Balanced");
    			else ImGui::Text("Quality");
            }
            
            // Limit Info
             ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Status");
            ImGui::TableSetColumnIndex(1);
    		if (settings.FPSCap) 
    		{ 
    			if (settings.CapMode == FrameGeneration::FrameGenSettings::FpsCapMode::Native) ImGui::Text("Limited (Native)");
    			else ImGui::Text("Limited (Display)");
    		}
    		else ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Unlimited");

            ImGui::EndTable();
        }

        // ----------------- PLOT -----------------
		ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
		ImGui::PlotLines("##FrameTimes", frameTimes, 120, frameTimeOffset, nullptr, 0.0f, 33.0f, ImVec2(220, 35));
        ImGui::PopStyleColor(2);

		ImGui::End();
	}
	ImGui::PopStyleColor(); // Pop WindowBg
	ImGui::PopStyleVar(3);
}
