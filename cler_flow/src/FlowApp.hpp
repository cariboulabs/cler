/******************************************************************************************
*                                                                                         *
*    FlowApp - Main application class for CLER Flow                                      *
*                                                                                         *
*    Modernized version of MyApp from core-nodes                                         *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "GuiApp.hpp"
#include "FlowCanvas.hpp"
#include "BlockLibrary.hpp"
#include <memory>
#include <filesystem>
#include <deque>

namespace clerflow {

class FlowApp : public GuiApp
{
public:
    FlowApp();
    ~FlowApp() = default;
    
    void Update() override;
    
private:
    // Application state
    std::string version{"v0.1.0"};
    bool initialSetup = false;
    
    // Core components
    std::unique_ptr<FlowCanvas> flowCanvas;
    std::unique_ptr<BlockLibrary> blockLibrary;
    
    // Docking state
    bool openDockspace = true;
    bool redock = false;
    void Dockspace();
    
    // Menu handling
    void Menu();
    void MenuFile();
    void MenuView();
    void MenuHelp();
    
    // File management
    bool hasFile = false;
    std::filesystem::path filePath;
    void NewProject();
    void OpenProject();
    void SaveProject(bool saveAs = false);
    void SaveToFile(const std::string& name, const std::string& path);
    void LoadFromFile(const std::string& path);
    
    // Windows
    void DrawCanvas();
    void DrawLibrary();
    void DrawProperties();
    void DrawCodePreview();
    
    // Utility
    void SelectTab(const char* windowName) const;
};

} // namespace clerflow