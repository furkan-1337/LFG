# Lufzy's Frame Generation

**LFG** (Lufzy's Frame Generation) is an experimental Frame Generation pipeline designed for DirectX 11 applications. It intercepts the game's rendering pipeline to inject intermediate frames generated using Optical Flow and Frame Interpolation techniques, significantly improving perceived smoothness and framerates.

## 🚀 Features

### ⚡ Frame Generation
- **Multi-Frame Generation**: Support for 2x, 3x, and 4x framerate multiplication.
- **Dynamic Mode**: Automatically adjusts the generation ratio to maintain a specified Target FPS.
- **Low Latency Mode**: Integrated Reflex-like behavior to minimize input lag.

### 🌊 Optical Flow
Advanced motion estimation using Compute Shaders:
- **Block Matching**: Efficient basic motion estimation.
- **Farneback**: Dense optical flow for smoother motion fields.
- **DIS (Dense Inverse Search)**: High-performance flow algorithm.
- **Hierarchical Search**: Pyramid-based processing (Coarse-to-Fine) for capturing large motions.
- **Bi-Directional Flow**: Forward and backward flow estimation for higher quality interpolation.

### 🖼️ Upscaling & Post-Processing
- **Upscaling**: Integrated high-quality upscalers:
  - Lanczos (Configurable Radius)
  - Bicubic, Bilinear, Nearest
- **RCAS**: Robust Contrast Adaptive Sharpening for crisp visuals.
- **Artifact Reduction**: Ghosting reduction and Edge Protection (Sobel) algorithms.
- **Motion Smoothing**: Post-process vector smoothing for cleaner interpolation.

### 🛠️ Developer Tools
- **Split-Screen Comparison**: Real-time side-by-side view of Native vs. Generated output.
- **Debug Overlay**: Visualize Motion Vectors, HUD Masks, and Edge Detection in real-time.
- **ImGui Menu**: comprehensive in-game configuration overlay.

## 🔧 Technology Stack

- **Core**: C++ (std20)
- **Graphics**: DirectX 11
- **Shaders**: HLSL 5.0 (Compute Shaders)
- **Hooking**: MinHook (Present / ResizeBuffers)
- **UI**: ImGui

## 🔨 Build Instructions

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/furkan-1337/LFG.git
    ```
2.  Open `LFG.sln` in **Visual Studio 2022**.
3.  Select build configuration: **Release** / **x64**.
4.  Build the solution.
5.  The output `LFG.dll` will be compiling to the `x64/Release` folder.

## 🎮 Usage

1.  Use any standard DLL Injector (e.g., Xenos, Extreme Injector).
2.  Target a **64-bit DirectX 11** game process.
3.  Inject `LFG.dll`.
4.  Press **INSERT** to open the overlay menu.

## 📝 Disclaimer

This project is for **educational and research purposes**. It involves hooking into graphics APIs and modifying game behavior. Use at your own risk, especially in online games with anti-cheat systems.

