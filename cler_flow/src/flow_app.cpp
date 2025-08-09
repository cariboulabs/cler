/******************************************************************************************
*                                                                                         *
*    FlowApp - Main application implementation                                           *
*                                                                                         *
******************************************************************************************/

#include "flow_app.hpp"
#include <imgui_internal.h>  // For docking functions
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
    
#ifdef HAS_LIBCLANG
    // Auto-load desktop blocks on startup
    blockLibrary->StartLoadingDesktopBlocks();
    // Only show popup if we're actually loading (not from cache)
    if (blockLibrary->IsLoading()) {
        show_import_popup = true;  // Show progress popup
    }
#endif
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

        ImGui::DockBuilderDockWindow("Library", dock_id_left_top);
        ImGui::DockBuilderDockWindow("Code Preview", dock_main_id);  // Dock Code Preview first
        ImGui::DockBuilderDockWindow("Canvas", dock_main_id);        // Then dock Canvas - last one docked becomes active
        ImGui::DockBuilderDockWindow("Properties", dock_id_left_bottom);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    Menu();
    ImGui::End();
    
    // Main windows
    DrawLibrary();
    DrawProperties();
    DrawCodePreview();
    DrawCanvas();  // Draw Canvas last so it's on top
    
    // Check if we need to show import popup
#ifdef HAS_LIBCLANG
    if (show_import_popup && !ImGui::IsPopupOpen("Import Progress")) {
        ImGui::OpenPopup("Import Progress");
    }
    
    // Clear the flag after loading is done
    if (!blockLibrary->IsLoading() && show_import_popup) {
        show_import_popup = false;
    }
#endif
    
    // Draw popups last so they appear on top of everything
    DrawImportProgress();
    
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
        
        ImGui::EndMenuBar();
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
        if (ImGui::MenuItem("Keyboard Shortcuts")) {
            showShortcuts = true;
        }
        
        if (ImGui::MenuItem("About")) {
            showAbout = true;
        }
        
        if (ImGui::MenuItem("Documentation")) {
            // TODO: Open documentation
        }
        
        ImGui::EndMenu();
    }
    
    // Shortcuts popup
    if (showShortcuts) {
        ImGui::OpenPopup("Shortcuts");
        showShortcuts = false;
    }
    
    if (ImGui::BeginPopupModal("Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Keyboard Shortcuts");
        ImGui::Separator();
        
        ImGui::Text("Canvas Navigation:");
        ImGui::BulletText("Middle Mouse + Drag: Pan canvas");
        ImGui::BulletText("Mouse Wheel: Zoom in/out");
        ImGui::Spacing();
        
        ImGui::Text("Selection:");
        ImGui::BulletText("Left Click: Select node");
        ImGui::BulletText("Shift + Left Click: Add to selection");
        ImGui::BulletText("Ctrl + A: Select all nodes");
        ImGui::BulletText("Left Click + Drag (empty space): Box select");
        ImGui::Spacing();
        
        ImGui::Text("Node Operations:");
        ImGui::BulletText("Delete: Delete selected nodes");
        ImGui::BulletText("R: Rotate selected nodes right (90°)");
        ImGui::BulletText("Shift + R: Rotate selected nodes left (90°)");
        ImGui::BulletText("Left Click + Drag (on node): Move selected nodes");
        ImGui::Spacing();
        
        ImGui::Text("File Operations:");
        ImGui::BulletText("Ctrl + N: New project");
        ImGui::BulletText("Ctrl + O: Open project");
        ImGui::BulletText("Ctrl + S: Save project");
        ImGui::BulletText("Ctrl + Shift + S: Save as");
        ImGui::BulletText("Ctrl + G: Generate C++ code");
        ImGui::Spacing();
        
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // About popup
    if (showAbout) {
        ImGui::OpenPopup("About");
        showAbout = false;
    }
    
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

void FlowApp::ImportBlocks()
{
    // This function is no longer used since we auto-load on startup
    // Keeping it for potential future use with file dialogs
}

void FlowApp::DrawImportProgress()
{
#ifdef HAS_LIBCLANG
    // Process blocks while loading (this is where the magic happens!)
    if (blockLibrary->IsLoading()) {
        blockLibrary->ProcessNextBlocks(1); // Process 1 file per frame for responsive UI
    }
    
    // Get the main viewport to center in the entire application window
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImVec2 popup_size(450, 220);
    
    // Center the popup in the main application window
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(popup_size, ImGuiCond_Always);
    
    // Style the popup
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    
    // Set dimming of background
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
    
    if (ImGui::BeginPopupModal("Import Progress", nullptr, 
                               ImGuiWindowFlags_NoResize | 
                               ImGuiWindowFlags_NoMove | 
                               ImGuiWindowFlags_NoTitleBar | 
                               ImGuiWindowFlags_NoScrollbar)) {
        
        // Title - clean and centered
        const char* title = "Importing Blocks";
        float title_width = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX((popup_size.x - title_width) * 0.5f - 20);
        ImGui::Text("%s", title);
        
        ImGui::Separator();
        ImGui::Spacing();
        
        if (blockLibrary->IsLoading()) {
            // Current block being processed
            if (!blockLibrary->GetCurrentBlock().empty()) {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), 
                                  "Importing: %s", blockLibrary->GetCurrentBlock().c_str());
            } else if (!blockLibrary->GetCurrentFile().empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f),
                                  "Scanning: %s", blockLibrary->GetCurrentFile().c_str());
            } else {
                ImGui::Text("Scanning for blocks...");
            }
            
            ImGui::Spacing();
            
            // Progress bar
            float progress = blockLibrary->GetLoadProgress();
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.0f%%", progress * 100.0f);
            ImGui::ProgressBar(progress, ImVec2(-1, 20), overlay);
            
            ImGui::Spacing();
            
            // Statistics
            ImGui::Text("Files: %d / %d   Blocks found: %d", 
                       blockLibrary->GetFilesScanned(), 
                       blockLibrary->GetTotalFiles(),
                       blockLibrary->GetBlocksFound());
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            // Cancel button (right-aligned)
            float button_width = 80;
            ImGui::SetCursorPosX(popup_size.x - button_width - 20);
            
            if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
                blockLibrary->CancelLoading();
                ImGui::CloseCurrentPopup();
            }
            
        } else {
            // Loading complete
            ImGui::Spacing();
            
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Import Complete!");
            
            ImGui::Spacing();
            
            ImGui::Text("%s", blockLibrary->GetLoadStatus().c_str());
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            // OK button (centered)
            float button_width = 80;
            ImGui::SetCursorPosX((popup_size.x - button_width) * 0.5f - 20);
            
            if (ImGui::Button("OK", ImVec2(button_width, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
        }
        
        ImGui::EndPopup();
    }
    
    ImGui::PopStyleColor(); // ModalWindowDimBg
    ImGui::PopStyleVar(2); // WindowRounding and WindowPadding
#else
    // Show message when libclang is not available
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Import Not Available", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Block import requires libclang to parse C++ headers.");
        ImGui::Text("Please rebuild with libclang support enabled.");
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
#endif
}

void FlowApp::DrawCanvas()
{
    ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    
    // Focus canvas on first frame
    if (firstFrame) {
        ImGui::SetWindowFocus();
        firstFrame = false;
    }
    
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
    ImGui::Begin("Library", nullptr, ImGuiWindowFlags_None);
    
    blockLibrary->Draw(flowCanvas.get());
    
    ImGui::End();
}

void FlowApp::DrawProperties()
{
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_None);
    
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
    
    // Show feedback if user tries to drop a block here
    if (ImGui::IsWindowHovered() && ImGui::GetDragDropPayload()) {
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload()) {
            if (payload->IsDataType("BLOCK_SPEC")) {
                // Show "not allowed" feedback
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 mouse_pos = ImGui::GetMousePos();
                
                // Draw red X
                draw_list->AddLine(
                    ImVec2(mouse_pos.x - 15, mouse_pos.y - 15),
                    ImVec2(mouse_pos.x + 15, mouse_pos.y + 15),
                    IM_COL32(255, 50, 50, 200), 3.0f
                );
                draw_list->AddLine(
                    ImVec2(mouse_pos.x - 15, mouse_pos.y + 15),
                    ImVec2(mouse_pos.x + 15, mouse_pos.y - 15),
                    IM_COL32(255, 50, 50, 200), 3.0f
                );
            }
        }
    }
    
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