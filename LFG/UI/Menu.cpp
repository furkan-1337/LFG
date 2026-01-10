#include "Menu.h"
#include <Dependencies/ImGui/imgui.h>
#include "../Pipeline/Generation/FrameGeneration.h"

// Helper to check if current settings match a preset
static int GetBestMatchingPreset(const FrameGeneration::FrameGenSettings& s)
{
    // 0: Ultra Performance
    // Check all relevant settings for Ultra Performance preset
    if (s.RenderScale == 0.33f && s.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Nearest && 
        s.EnableAggressiveDynamicMode &&
        !s.EnableBiDirFlow && !s.EnableAdaptiveBlock && s.OpticalFlowAlgorithm == 0 && 
        s.BlockSize == 32 && s.SearchRadius == 4 && s.MaxPyramidLevel == 2 && s.MinPyramidLevel == 2 && 
        !s.EnableSubPixel && !s.EnableMotionSmoothing &&
        s.RcasStrength == 0.0f && s.GhostingReduction == 0.0f && !s.EnableEdgeProtection &&
        s.EnableAsyncCompute && s.LowLatencyMode && s.DisableVSync) return 0;
        
    // 1: Performance
    // Check all relevant settings for Performance preset
    if (s.RenderScale == 0.5f && s.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Bilinear && 
        !s.EnableAggressiveDynamicMode &&
        !s.EnableBiDirFlow && !s.EnableAdaptiveBlock && s.OpticalFlowAlgorithm == 0 && 
        s.BlockSize == 16 && s.SearchRadius == 8 && s.MaxPyramidLevel == 1 && s.MinPyramidLevel == 1 && 
        !s.EnableSubPixel && !s.EnableMotionSmoothing &&
        s.RcasStrength == 0.2f && s.GhostingReduction == 0.1f && !s.EnableEdgeProtection) return 1;

    // 2: Balanced
    // Check all relevant settings for Balanced preset
    if (s.RenderScale == 0.67f && s.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Bicubic && 
        !s.EnableAggressiveDynamicMode &&
        !s.EnableBiDirFlow && s.EnableAdaptiveBlock && s.OpticalFlowAlgorithm == 1 && 
        s.BlockSize == 16 && s.SearchRadius == 16 && s.MaxPyramidLevel == 1 && s.MinPyramidLevel == 0 && 
        s.EnableSubPixel && !s.EnableMotionSmoothing &&
        s.RcasStrength == 0.5f && s.GhostingReduction == 0.3f && s.EnableEdgeProtection) return 2;

    // 3: Quality
    // Check all relevant settings for Quality preset
    if (s.RenderScale == 0.85f && s.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Lanczos && 
        s.LanczosRadius == 2 && !s.EnableAggressiveDynamicMode &&
        s.EnableBiDirFlow && s.EnableAdaptiveBlock && s.OpticalFlowAlgorithm == 1 && 
        s.BlockSize == 8 && s.SearchRadius == 24 && s.MaxPyramidLevel == 1 && s.MinPyramidLevel == 0 && 
        s.EnableSubPixel && s.EnableMotionSmoothing &&
        s.RcasStrength == 0.7f && s.GhostingReduction == 0.5f && s.EnableEdgeProtection) return 3;

    // 4: Cinematic
    // Check all relevant settings for Cinematic preset
    if (s.RenderScale == 1.0f && s.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Lanczos && 
        s.LanczosRadius == 3 && !s.EnableAggressiveDynamicMode &&
        s.EnableBiDirFlow && s.EnableAdaptiveBlock && s.OpticalFlowAlgorithm == 1 && // Note: Original had DIS, but then changed to Farneback. Using Farneback for preset check.
        s.BlockSize == 4 && s.SearchRadius == 32 && s.MaxPyramidLevel == 0 && s.MinPyramidLevel == 0 && 
        s.EnableSubPixel && s.EnableMotionSmoothing &&
        s.RcasStrength == 0.9f && s.GhostingReduction == 0.8f && s.EnableEdgeProtection) return 4;

    return -1; // Custom
}

void UI::Menu::Render(bool& open)
{
	if (!open) return;

	if (ImGui::Begin("Lufzy's Frame Generation", &open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		bool isEnabled = FrameGeneration::Instance().IsEnabled();
		if (ImGui::Checkbox("Enable", &isEnabled))
		{
			FrameGeneration::Instance().SetEnabled(isEnabled);
		}
		
		ImGui::Separator();
		auto& settings = FrameGeneration::Instance().GetSettings();

        // ---------------------------------------------------------
        // PRESETS (Common)
        // ---------------------------------------------------------
        // Sync UI state with actual settings
        static int preset = 2; // Default Balanced
        int detected = GetBestMatchingPreset(settings);
        
        if (preset != detected)
        {
            preset = detected; 
        }

        const char* presets[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Cinematic", "Custom" };
        int currentComboValue = (preset == -1) ? 5 : preset; // 5 is Custom index
        
        if (ImGui::Combo("Performance Profile", &currentComboValue, presets, IM_ARRAYSIZE(presets)))
        {
            if (currentComboValue != 5) // If not Custom
            {
                preset = currentComboValue;
                // Apply Preset
                if (preset == 0) // Ultra Performance
                {
                    settings.RenderScale = 0.33f;
                    settings.UpscaleMode = FrameGeneration::FrameGenSettings::UpscaleType::Nearest;
                    settings.EnableAggressiveDynamicMode = true; 
                    
                    settings.EnableBiDirFlow = false;
                    settings.EnableAdaptiveBlock = false;
                    settings.OpticalFlowAlgorithm = 0; // Block Matching
                    settings.BlockSize = 32; settings.SearchRadius = 4;
                    settings.MaxPyramidLevel = 2; settings.MinPyramidLevel = 2; // Coarse
                    settings.EnableSubPixel = false;
                    settings.EnableMotionSmoothing = false;
                    
                    settings.RcasStrength = 0.0f;
                    settings.GhostingReduction = 0.0f;
                    settings.EnableEdgeProtection = false;
                    
                    settings.EnableAsyncCompute = true;
                    settings.LowLatencyMode = true;
                    settings.DisableVSync = true;
                }
                else if (preset == 1) // Performance
                {
                    settings.RenderScale = 0.5f;
                    settings.UpscaleMode = FrameGeneration::FrameGenSettings::UpscaleType::Bilinear;
                    settings.EnableAggressiveDynamicMode = false;
                    
                    settings.EnableBiDirFlow = false;
                    settings.EnableAdaptiveBlock = false;
                    settings.OpticalFlowAlgorithm = 0; // Block Matching
                    settings.BlockSize = 16; settings.SearchRadius = 8;
                    settings.MaxPyramidLevel = 1; settings.MinPyramidLevel = 1; // Half Res
                    settings.EnableSubPixel = false;
                    settings.EnableMotionSmoothing = false;
                    
                    settings.GhostingReduction = 0.1f;
                    settings.EnableEdgeProtection = false;
                }
                else if (preset == 2) // Balanced
                {
                    settings.RenderScale = 0.67f;
                    settings.UpscaleMode = FrameGeneration::FrameGenSettings::UpscaleType::Bicubic;
                    settings.EnableAggressiveDynamicMode = false;
                    
                    settings.EnableBiDirFlow = false;
                    settings.EnableAdaptiveBlock = true; // [Adaptive]
                    settings.OpticalFlowAlgorithm = 1; // Farneback (Smoother)
                    settings.BlockSize = 16; settings.SearchRadius = 16;
                    settings.MaxPyramidLevel = 1; settings.MinPyramidLevel = 0; // Refine to Native
                    settings.EnableSubPixel = true;
                    settings.EnableMotionSmoothing = false;
                    
                    settings.GhostingReduction = 0.3f;
                    settings.EnableEdgeProtection = true;
                }
                else if (preset == 3) // Quality
                {
                    settings.RenderScale = 0.85f; // High but not full
                    settings.UpscaleMode = FrameGeneration::FrameGenSettings::UpscaleType::Lanczos;
                    settings.LanczosRadius = 2;
                    settings.EnableAggressiveDynamicMode = false;
                    
                    settings.EnableBiDirFlow = true; // [Bi-Dir]
                    settings.EnableAdaptiveBlock = true;
                    settings.OpticalFlowAlgorithm = 1; // Farneback
                    settings.BlockSize = 8; settings.SearchRadius = 24;
                    settings.MaxPyramidLevel = 1; settings.MinPyramidLevel = 0;
                    settings.EnableSubPixel = true;
                    settings.EnableMotionSmoothing = true; // [Motion Smooth]
                    
                    settings.GhostingReduction = 0.5f;
                    settings.EnableEdgeProtection = true;
                }
                else if (preset == 4) // Cinematic
                {
                    settings.RenderScale = 1.0f; // Native
                    settings.UpscaleMode = FrameGeneration::FrameGenSettings::UpscaleType::Lanczos;
                    settings.LanczosRadius = 3; // Max Sharpness
                    settings.EnableAggressiveDynamicMode = false;
                    
                    settings.EnableBiDirFlow = true;
                    settings.EnableAdaptiveBlock = true;
                    settings.OpticalFlowAlgorithm = 1; // Farneback (Still using Farneback as per safe default logic)
                    
                    settings.BlockSize = 4; // Detail
                    settings.SearchRadius = 32; // Wide
                    settings.MaxPyramidLevel = 0; settings.MinPyramidLevel = 0; // Full Search
                    settings.EnableSubPixel = true;
                    settings.EnableMotionSmoothing = true;
                    
                    settings.GhostingReduction = 0.8f;
                    settings.EnableEdgeProtection = true;
                }
            }
        }
        
        ImGui::Dummy(ImVec2(0, 5));
        
        // ---------------------------------------------------------
        // DETAILED TABS
        // ---------------------------------------------------------
		if (ImGui::BeginTabBar("LFG_Tabs"))
		{
			// [Tab 1: Pipeline]
			if (ImGui::BeginTabItem("Pipeline"))
			{
                ImGui::Spacing();
                ImGui::Text("Optical Flow");
				const char* flowAlgos[] = { "Block Matching", "Farneback", "DIS" };
				ImGui::Combo("Algorithm", &settings.OpticalFlowAlgorithm, flowAlgos, IM_ARRAYSIZE(flowAlgos));
                
				ImGui::SliderInt("Block Size", &settings.BlockSize, 4, 32);
				ImGui::SliderInt("Search Radius", &settings.SearchRadius, 4, 32);
				ImGui::Checkbox("Sub-Pixel Flow", &settings.EnableSubPixel);
                
                ImGui::Text("Pyramid Levels");
                ImGui::SliderInt("Start Level", &settings.MaxPyramidLevel, 0, 4, "Level %d");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = Full Res (Slow), 1 = Half, 2 = Quarter...\nHigher start means coarser initial search.");
                
                ImGui::SliderInt("End Level", &settings.MinPyramidLevel, 0, settings.MaxPyramidLevel, "Level %d");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lowest level to search.\n0 = Refine to Full Res, 1 = Stop at Half Res.");
                
				ImGui::Separator();
				ImGui::Text("Resolution & Scaling");
				
                const char* upscaleModes[] = { "Native", "Nearest", "Bilinear", "Bicubic", "Lanczos" };
				int currentMode = (int)settings.UpscaleMode;
				if (ImGui::Combo("Upscale Method", &currentMode, upscaleModes, IM_ARRAYSIZE(upscaleModes)))
                {
					settings.UpscaleMode = (FrameGeneration::FrameGenSettings::UpscaleType)currentMode;
                    // Force Native Scale if Native Mode selected
                    if (settings.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Native)
                    {
                        settings.RenderScale = 1.0f;
                    }
                }

                // Only show Render Scale if NOT Native
                if (settings.UpscaleMode != FrameGeneration::FrameGenSettings::UpscaleType::Native)
                {
    				if (ImGui::SliderFloat("Render Scale", &settings.RenderScale, 0.1f, 1.0f, "%.2fx"))
    				{
    					if (settings.RenderScale > 0.99f) settings.RenderScale = 1.0f;
    				}
                }
                    
				if (settings.UpscaleMode == FrameGeneration::FrameGenSettings::UpscaleType::Lanczos)
					ImGui::SliderInt("Lanczos Radius", &settings.LanczosRadius, 1, 4);
                    
                ImGui::Separator();
                ImGui::Text("Advanced Quality");
                ImGui::Checkbox("Bi-Directional Flow", &settings.EnableBiDirFlow);
				ImGui::Checkbox("Adaptive Block Size", &settings.EnableAdaptiveBlock);
				ImGui::Checkbox("Motion Smoothing", &settings.EnableMotionSmoothing); // Post-process vector smooth

                ImGui::EndTabItem();
            }

			// [Tab 2: Post-Process]
			if (ImGui::BeginTabItem("PostFX"))
			{
				ImGui::Spacing();
				ImGui::Text("Visual Enhancements");

				ImGui::SliderFloat("Sharpening (RCAS)", &settings.RcasStrength, 0.0f, 1.0f, "%.2f");
				ImGui::SliderFloat("Ghosting Reduction", &settings.GhostingReduction, 0.0f, 1.0f, "%.2f");
				
				ImGui::Separator();
				ImGui::Checkbox("Edge Protection (Sobel)", &settings.EnableEdgeProtection);
				ImGui::SliderInt("Scene Change Threshold", &settings.SceneChangeThreshold, 0, 5000);

				ImGui::EndTabItem();
			}
            
            // [Tab 3: System]
            if (ImGui::BeginTabItem("System"))
            {
                ImGui::Spacing();
                ImGui::Text("Generation");
                const char* multiModes[] = { "Off", "2x (1 Frame)", "3x (2 Frames)", "4x (3 Frames)", "Dynamic" };
                int currentGenMode = settings.EnableDynamicRatio ? 4 : settings.MultiFrameCount;
                
                if (ImGui::Combo("Generation Mode", &currentGenMode, multiModes, IM_ARRAYSIZE(multiModes)))
                {
                    if (currentGenMode == 4)
                    {
                        settings.EnableDynamicRatio = true;
                    }
                    else
                    {
                        settings.EnableDynamicRatio = false;
                        settings.MultiFrameCount = currentGenMode;
                        settings.EnableAggressiveDynamicMode = false;
                    }
                }

				if (settings.EnableDynamicRatio)
				{
					ImGui::SliderInt("Dynamic Target", &settings.DynamicTargetFPS, 30, 1000);
                    
                    // Added Aggressive Mode Checkbox here as well
                    ImGui::Checkbox("Aggressive Mode (6x)", &settings.EnableAggressiveDynamicMode);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Allows generating up to 6 frames if needed to reach target.");
				}

                ImGui::Separator();

                ImGui::Checkbox("Limit FPS", &settings.FPSCap);
				if (settings.FPSCap)
				{
					ImGui::SliderInt("Limit", &settings.TargetFPS, 0, 1000);
                    const char* capModes[] = { "Native", "Display" };
					int currentMode = (int)settings.CapMode;
					if (ImGui::Combo("Cap Mode", &currentMode, capModes, IM_ARRAYSIZE(capModes)))
						settings.CapMode = (FrameGeneration::FrameGenSettings::FpsCapMode)currentMode;
                }   
                
                ImGui::Separator();
                ImGui::Checkbox("Disable VSync", &settings.DisableVSync);
                ImGui::Checkbox("Low Latency Mode", &settings.LowLatencyMode);
                ImGui::Checkbox("Async Compute", &settings.EnableAsyncCompute);
                
                ImGui::EndTabItem();
            }

			// [Tab 4: Debug]
			if (ImGui::BeginTabItem("Debug"))
			{
				ImGui::Checkbox("Split-Screen Comparison", &settings.EnableSplitScreen);
				if (settings.EnableSplitScreen) ImGui::SliderFloat("Split Pos", &settings.SplitScreenPosition, 0.0f, 1.0f);

				ImGui::Checkbox("Overlay", &settings.ShowDebugOverlay);
				const char* debugModes[] = { "Off", "Motion Vectors", "HUD Mask" };
				ImGui::Combo("View Mode", &settings.DebugViewMode, debugModes, IM_ARRAYSIZE(debugModes));

				ImGui::SliderFloat("Motion Sensitivity", &settings.MotionSensitivity, 0.1f, 5.0f, "%.1f");
				ImGui::SliderFloat("HUD Threshold", &settings.HUDThreshold, 0.0f, 0.2f, "%.3f");

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
        }
        
        ImGui::Dummy(ImVec2(0, 5));
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.8f), "github.com/furkan-1337");
	}
	ImGui::End();
}
