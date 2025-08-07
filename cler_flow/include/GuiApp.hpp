/******************************************************************************************
*                                                                                         *
*    GuiApp - Base class for ImGui applications                                          *
*                                                                                         *
*    Simplified version from gui-app-template                                            *
*                                                                                         *
******************************************************************************************/

#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace clerflow {

class GuiApp
{
public:
    GuiApp(const std::string& title = "CLER Flow", int width = 1280, int height = 720);
    virtual ~GuiApp();
    
    // Main loop
    void Run();
    
    // Override this for application logic
    virtual void Update() = 0;
    
protected:
    // Window management
    struct GLFWwindow* window = nullptr;
    std::string windowTitle;
    int windowWidth;
    int windowHeight;
    
    // ImGui state
    bool showDemoWindow = false;
    bool showMetrics = false;
    
    // Initialize/cleanup
    bool Initialize();
    void Cleanup();
    
    // Frame management
    void BeginFrame();
    void EndFrame();
    
    // Font loading
    void LoadFonts();
};

} // namespace clerflow