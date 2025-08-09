/******************************************************************************************
*                                                                                         *
*    FlowApp - Main application class for CLER Flow                                      *
*                                                                                         *
*    Modernized version of MyApp from core-nodes                                         *
*                                                                                         *
******************************************************************************************/

#pragma once

#include "gui_app.hpp"
#include "flow_canvas.hpp"
#include "block_library.hpp"
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
    
    // UI state
    bool showDemoWindow = false;
    bool showMetrics = false;
    bool showShortcuts = false;
    bool showAbout = false;
    bool firstFrame = true;
    
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
    void ImportBlocks();
    
    // Windows
    void DrawCanvas();
    void DrawLibrary();
    void DrawProperties();
    void DrawCodePreview();
    
    // Utility
    void SelectTab(const char* windowName) const;
    void DrawImportProgress();
};

} // namespace clerflow