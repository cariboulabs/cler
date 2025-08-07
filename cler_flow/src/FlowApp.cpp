/******************************************************************************************
*                                                                                         *
*    FlowApp - Main application implementation                                           *
*                                                                                         *
******************************************************************************************/

#include "FlowApp.hpp"
#include <iostream>
#include <fstream>

namespace clerflow {

FlowApp::FlowApp() 
    : GuiApp("CLER Flow - Visual Flowgraph Designer", 1400, 900)
{
    // Initialize components
    flowCanvas = std::make_unique<FlowCanvas>();
    blockLibrary = std::make_unique<BlockLibrary>();
    
    // Load test blocks for development
    blockLibrary->LoadTestBlocks();
}

void FlowApp::Update()
{
    // Setup dockspace exactly like core-nodes
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", &openDockspace, window_flags);
    ImGui::PopStyleVar(3);

    // Setup initial docking layout like core-nodes
    if (ImGui::DockBuilderGetNode(ImGui::GetID("MyDockspace")) == nullptr || redock)
    {
        redock = false;
        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        ImGui::DockBuilderRemoveNode(dockspace_id); // Clear out existing layout
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace); // Add empty node
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_id_left_top = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.20f, nullptr, &dock_main_id);
        ImGuiID dock_id_left_bottom = ImGui::DockBuilderSplitNode(dock_id_left_top, ImGuiDir_Down, 0.60f, nullptr, &dock_id_left_top);
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

        ImGui::DockBuilderDockWindow("Library", dock_id_left_top);
        ImGui::DockBuilderDockWindow("Canvas", dock_main_id);
        ImGui::DockBuilderDockWindow("Properties", dock_id_left_bottom);
        ImGui::DockBuilderDockWindow("Code Preview", dock_id_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    Menu();
    ImGui::End();
    
    // Main windows
    DrawLibrary();
    DrawCanvas();
    DrawProperties();
    DrawCodePreview();
    
    // Demo window for debugging
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
}

void FlowApp::Menu()
{
    if (ImGui::BeginMenuBar()) {
        MenuFile();
        MenuView();
        MenuHelp();
        
        // Version info on the right
        float textWidth = ImGui::CalcTextSize(version.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - textWidth - 10);
        ImGui::TextDisabled("%s", version.c_str());
        
        ImGui::EndMainMenuBar();
    }
}

void FlowApp::MenuFile()
{
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            NewProject();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            OpenProject();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S", false, hasFile)) {
            SaveProject();
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            SaveProject(true);
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Import Block from Header...")) {
            // TODO: Open file dialog to select .hpp file
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Generate C++ Code", "Ctrl+G")) {
            // TODO: Generate code
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            // TODO: Check for unsaved changes
            std::exit(0);
        }
        
        ImGui::EndMenu();
    }
}

void FlowApp::MenuView()
{
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Layout")) {
            redock = true;
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
            // TODO: Implement zoom
        }
        if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
            // TODO: Implement zoom
        }
        if (ImGui::MenuItem("Reset Zoom", "Ctrl+0")) {
            // TODO: Implement zoom reset
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Show Demo Window", nullptr, &showDemoWindow)) {
        }
        if (ImGui::MenuItem("Show Metrics", nullptr, &showMetrics)) {
        }
        
        ImGui::EndMenu();
    }
    
    if (showMetrics) {
        ImGui::ShowMetricsWindow(&showMetrics);
    }
}

void FlowApp::MenuHelp()
{
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) {
            ImGui::OpenPopup("About");
        }
        
        if (ImGui::MenuItem("Documentation")) {
            // TODO: Open documentation
        }
        
        ImGui::EndMenu();
    }
    
    // About popup
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("CLER Flow - Visual Flowgraph Designer");
        ImGui::Text("Version %s", version.c_str());
        ImGui::Separator();
        ImGui::Text("A modern reconstruction of core-nodes");
        ImGui::Text("for CLER DSP flowgraph generation.");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void FlowApp::NewProject()
{
    // Check for unsaved changes
    if (hasFile) {
        // TODO: Ask to save
    }
    
    flowCanvas->ClearAll();
    hasFile = false;
    filePath.clear();
}

void FlowApp::OpenProject()
{
    // TODO: Implement file dialog
    // For now, just load a test file
}

void FlowApp::SaveProject(bool saveAs)
{
    if (!hasFile || saveAs) {
        // TODO: Show save dialog
        filePath = "untitled.flow";
        hasFile = true;
    }
    
    SaveToFile(filePath.filename().string(), filePath.parent_path().string());
}

void FlowApp::SaveToFile(const std::string& name, const std::string& path)
{
    std::filesystem::path fullPath = std::filesystem::path(path) / name;
    
    std::ofstream file(fullPath);
    if (file.is_open()) {
        file << flowCanvas->ToJSON();
        file.close();
        
        hasFile = true;
        filePath = fullPath;
    }
}

void FlowApp::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (file.is_open()) {
        std::string json((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
        file.close();
        
        flowCanvas->FromJSON(json);
        hasFile = true;
        filePath = path;
    }
}

void FlowApp::DrawCanvas()
{
    ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    
    flowCanvas->Draw();
    
    // Status bar
    ImGui::Separator();
    auto selected = flowCanvas->GetSelectedNodes();
    if (selected.empty()) {
        ImGui::Text("Ready");
    } else if (selected.size() == 1) {
        ImGui::Text("1 node selected");
    } else {
        ImGui::Text("%zu nodes selected", selected.size());
    }
    
    ImGui::End();
}

void FlowApp::DrawLibrary()
{
    ImGui::Begin("Block Library");
    
    blockLibrary->Draw(flowCanvas.get());
    
    ImGui::End();
}

void FlowApp::DrawProperties()
{
    ImGui::Begin("Properties");
    
    auto selected = flowCanvas->GetSelectedNodes();
    if (selected.size() == 1) {
        // TODO: Show properties for selected node
        ImGui::Text("Node Properties");
        ImGui::Separator();
        ImGui::Text("ID: %zu", selected[0]);
    } else if (selected.size() > 1) {
        ImGui::Text("%zu nodes selected", selected.size());
    } else {
        ImGui::TextDisabled("Select a node to view properties");
    }
    
    ImGui::End();
}

void FlowApp::DrawCodePreview()
{
    ImGui::Begin("Code Preview");
    
    ImGui::Text("Generated C++ Code:");
    ImGui::Separator();
    
    std::string code = flowCanvas->GenerateCppCode();
    if (!code.empty()) {
        // Use a monospace font for code
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextUnformatted(code.c_str());
        ImGui::PopFont();
    } else {
        ImGui::TextDisabled("No code to generate");
    }
    
    ImGui::End();
}

void FlowApp::SelectTab(const char* windowName) const
{
    ImGuiWindow* window = ImGui::FindWindowByName(windowName);
    if (window) {
        ImGui::FocusWindow(window);
    }
}

} // namespace clerflow