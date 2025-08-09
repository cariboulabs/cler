/******************************************************************************************
*                                                                                         *
*    BlockLibrary - Implementation of block management and UI browser                    *
*                                                                                         *
******************************************************************************************/

#include "block_library.hpp"
#include "flow_canvas.hpp"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace clerflow {

BlockLibrary::BlockLibrary()
{
    blocks_by_category["Sources"] = {};
    blocks_by_category["Sinks"] = {};
    blocks_by_category["Processing"] = {};
    blocks_by_category["Math"] = {};
    blocks_by_category["Utility"] = {};
}

void BlockLibrary::AddBlock(std::shared_ptr<BlockSpec> spec)
{
    all_blocks.push_back(spec);
    
    // Add to category
    if (blocks_by_category.find(spec->category) != blocks_by_category.end()) {
        blocks_by_category[spec->category].push_back(spec);
    }
    else {
        blocks_by_category["Utility"].push_back(spec);
    }
}

void BlockLibrary::ClearBlocks()
{
    all_blocks.clear();
    for (auto& [category, list] : blocks_by_category) {
        list.clear();
    }
}

void BlockLibrary::ImportFromHeader(const std::string& header_path)
{
    // TODO: Implement header parsing
    (void)header_path;
}

void BlockLibrary::ImportFromDirectory(const std::string& dir_path)
{
    // TODO: Implement directory scanning
    (void)dir_path;
}

void BlockLibrary::SetSearchFilter(const std::string& filter)
{
    search_filter = filter;
}

#ifdef HAS_LIBCLANG
void BlockLibrary::StartLoadingDesktopBlocks()
{
    // Reset state
    is_loading = true;
    cancel_requested = false;
    load_progress = 0.0f;
    files_scanned = 0;
    blocks_found = 0;
    current_file_index = 0;
    files_to_scan.clear();
    temp_parsed_blocks.clear();
    current_block_name.clear();
    
    load_status = "Scanning for block files...";
    
    // Collect all .hpp files from desktop_blocks
    namespace fs = std::filesystem;
    std::string desktop_blocks_path = "/home/alon/repos/cler/desktop_blocks";
    
    try {
        for (const auto& entry : fs::recursive_directory_iterator(desktop_blocks_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".hpp") {
                files_to_scan.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        load_status = std::string("Error: ") + e.what();
        is_loading = false;
        return;
    }
    
    total_files_to_scan = files_to_scan.size();
    load_status = "Found " + std::to_string(total_files_to_scan) + " header files";
}

void BlockLibrary::ProcessNextBlocks(int blocks_per_frame)
{
    if (!is_loading || cancel_requested) {
        if (cancel_requested) {
            is_loading = false;
            cancel_requested = false;
            load_status = "Import cancelled";
            current_block_name.clear();
        }
        return;
    }
    
    // Process files for this frame
    for (int i = 0; i < blocks_per_frame && current_file_index < files_to_scan.size() && !cancel_requested; ++i) {
        const std::string& file_path = files_to_scan[current_file_index];
        
        // Extract just the filename for display
        namespace fs = std::filesystem;
        current_file = fs::path(file_path).filename().string();
        load_status = "Processing: " + current_file;
        
        // Quick check if it's a block header
        BlockParser parser;
        if (parser.isBlockHeader(file_path)) {
            // Parse the header
            BlockMetadata metadata = parser.parseHeader(file_path);
            if (metadata.is_valid) {
                // Track current block being processed
                current_block_name = metadata.class_name;
                blocks_found++;
                // Extract category from path
                fs::path file(file_path);
                fs::path root("/home/alon/repos/cler/desktop_blocks");
                fs::path relative = fs::relative(file.parent_path(), root);
                
                if (relative == ".") {
                    metadata.category = "Uncategorized";
                } else {
                    // Convert path to category string
                    std::string category;
                    for (const auto& part : relative) {
                        if (!category.empty()) {
                            category += "/";
                        }
                        std::string part_str = part.string();
                        if (!part_str.empty()) {
                            // Capitalize first letter
                            part_str[0] = std::toupper(part_str[0]);
                        }
                        category += part_str;
                    }
                    metadata.category = category;
                }
                
                metadata.library_name = "Desktop Blocks";
                temp_parsed_blocks.push_back(metadata);
            }
        }
        
        current_file_index++;
        files_scanned++;
        load_progress = static_cast<float>(files_scanned) / static_cast<float>(total_files_to_scan);
    }
    
    // Check if we're done scanning
    if (current_file_index >= files_to_scan.size()) {
        // Convert all parsed blocks to BlockSpec
        load_status = "Finalizing...";
        parsed_blocks = std::move(temp_parsed_blocks);
        
        for (const auto& metadata : parsed_blocks) {
            auto spec = std::make_shared<BlockSpec>();
            spec->class_name = metadata.class_name;
            spec->display_name = metadata.class_name;
            spec->category = metadata.category.empty() ? "Uncategorized" : metadata.category;
            spec->header_file = metadata.header_path;
            
            // Convert template params
            for (const auto& tparam : metadata.template_params) {
                ParamSpec param;
                param.name = tparam.name;
                param.display_name = tparam.name;
                param.type = ParamType::String;
                param.default_value = tparam.default_value;
                spec->template_params.push_back(param);
            }
            
            // Convert constructor params
            for (const auto& cparam : metadata.constructor_params) {
                ParamSpec param;
                param.name = cparam.name;
                param.display_name = cparam.name;
                // Detect type from string
                if (cparam.type.find("float") != std::string::npos) {
                    param.type = ParamType::Float;
                } else if (cparam.type.find("int") != std::string::npos) {
                    param.type = ParamType::Int;
                } else if (cparam.type.find("bool") != std::string::npos) {
                    param.type = ParamType::Bool;
                } else if (cparam.type.find("string") != std::string::npos || 
                          cparam.type.find("char") != std::string::npos) {
                    param.type = ParamType::String;
                } else {
                    param.type = ParamType::String;
                }
                param.default_value = cparam.default_value;
                spec->constructor_params.push_back(param);
            }
            
            // Convert input ports
            for (const auto& channel : metadata.input_channels) {
                PortSpec port;
                port.name = channel.name;
                port.display_name = channel.name;
                port.cpp_type = channel.type;
                // Detect data type from string
                if (channel.type.find("float") != std::string::npos) {
                    port.data_type = DataType::Float;
                } else if (channel.type.find("double") != std::string::npos) {
                    port.data_type = DataType::Double;
                } else if (channel.type.find("complex") != std::string::npos) {
                    if (channel.type.find("float") != std::string::npos) {
                        port.data_type = DataType::ComplexFloat;
                    } else {
                        port.data_type = DataType::ComplexDouble;
                    }
                } else if (channel.type.find("int") != std::string::npos) {
                    port.data_type = DataType::Int;
                } else {
                    port.data_type = DataType::Custom;
                }
                spec->input_ports.push_back(port);
            }
            
            // Convert output ports
            for (const auto& channel : metadata.output_channels) {
                PortSpec port;
                port.name = channel.name;
                port.display_name = channel.name;
                port.cpp_type = channel.type;
                // Detect data type from string
                if (channel.type.find("float") != std::string::npos) {
                    port.data_type = DataType::Float;
                } else if (channel.type.find("double") != std::string::npos) {
                    port.data_type = DataType::Double;
                } else if (channel.type.find("complex") != std::string::npos) {
                    if (channel.type.find("float") != std::string::npos) {
                        port.data_type = DataType::ComplexFloat;
                    } else {
                        port.data_type = DataType::ComplexDouble;
                    }
                } else if (channel.type.find("int") != std::string::npos) {
                    port.data_type = DataType::Int;
                } else {
                    port.data_type = DataType::Custom;
                }
                spec->output_ports.push_back(port);
            }
            
            // Detect if source or sink
            spec->is_source = spec->input_ports.empty() && !spec->output_ports.empty();
            spec->is_sink = !spec->input_ports.empty() && spec->output_ports.empty();
            
            AddBlock(spec);
        }
        
        is_loading = false;
        load_status = "Import complete! Found " + std::to_string(parsed_blocks.size()) + " blocks";
        current_block_name.clear();
    }
}

void BlockLibrary::CancelLoading()
{
    cancel_requested = true;
}

void BlockLibrary::RefreshLibrary()
{
    // Clear existing parsed blocks
    for (auto it = all_blocks.begin(); it != all_blocks.end(); ) {
        if ((*it)->header_file.find("desktop_blocks") != std::string::npos) {
            it = all_blocks.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clear from categories
    for (auto& [category, blocks] : blocks_by_category) {
        blocks.erase(
            std::remove_if(blocks.begin(), blocks.end(),
                [](const auto& block) {
                    return block->header_file.find("desktop_blocks") != std::string::npos;
                }),
            blocks.end()
        );
    }
    
    // Reload
    StartLoadingDesktopBlocks();
}
#endif

void BlockLibrary::LoadTestBlocks()
{
    // Create some test blocks for Phase 1
    
    // Sine Source
    auto sine_source = std::make_shared<BlockSpec>();
    sine_source->class_name = "SineSource";
    sine_source->display_name = "Sine Source";
    sine_source->category = "Sources";
    sine_source->header_file = "cler/blocks/sources.hpp";
    sine_source->is_source = true;
    
    ParamSpec freq_param;
    freq_param.name = "frequency";
    freq_param.display_name = "Frequency";
    freq_param.type = ParamType::Float;
    freq_param.default_value = "1000.0";
    freq_param.tooltip = "Frequency in Hz";
    freq_param.min = 0.1;
    freq_param.max = 20000.0;
    sine_source->constructor_params.push_back(freq_param);
    
    ParamSpec sr_param;
    sr_param.name = "sample_rate";
    sr_param.display_name = "Sample Rate";
    sr_param.type = ParamType::Float;
    sr_param.default_value = "48000.0";
    sr_param.tooltip = "Sample rate";
    sine_source->constructor_params.push_back(sr_param);
    
    PortSpec sine_out;
    sine_out.name = "out";
    sine_out.display_name = "Output";
    sine_out.data_type = DataType::Float;
    sine_out.cpp_type = "float";
    sine_source->output_ports.push_back(sine_out);
    
    AddBlock(sine_source);
    
    // Noise Source
    auto noise_source = std::make_shared<BlockSpec>();
    noise_source->class_name = "NoiseSource";
    noise_source->display_name = "Noise Source";
    noise_source->category = "Sources";
    noise_source->header_file = "cler/blocks/sources.hpp";
    noise_source->is_source = true;
    
    ParamSpec amp_param;
    amp_param.name = "amplitude";
    amp_param.display_name = "Amplitude";
    amp_param.type = ParamType::Float;
    amp_param.default_value = "0.5";
    amp_param.tooltip = "Signal amplitude";
    amp_param.min = 0.0;
    amp_param.max = 1.0;
    noise_source->constructor_params.push_back(amp_param);
    
    PortSpec noise_out;
    noise_out.name = "out";
    noise_out.display_name = "Output";
    noise_out.data_type = DataType::Float;
    noise_out.cpp_type = "float";
    noise_source->output_ports.push_back(noise_out);
    
    AddBlock(noise_source);
    
    // File Sink
    auto file_sink = std::make_shared<BlockSpec>();
    file_sink->class_name = "FileSink";
    file_sink->display_name = "File Sink";
    file_sink->category = "Sinks";
    file_sink->header_file = "cler/blocks/sinks.hpp";
    file_sink->is_sink = true;
    
    ParamSpec file_param;
    file_param.name = "filename";
    file_param.display_name = "Filename";
    file_param.type = ParamType::String;
    file_param.default_value = "output.dat";
    file_param.tooltip = "Output filename";
    file_sink->constructor_params.push_back(file_param);
    
    PortSpec file_in;
    file_in.name = "in";
    file_in.display_name = "Input";
    file_in.data_type = DataType::Float;
    file_in.cpp_type = "float";
    file_sink->input_ports.push_back(file_in);
    
    AddBlock(file_sink);
    
    // Multiply Block
    auto multiply = std::make_shared<BlockSpec>();
    multiply->class_name = "Multiply";
    multiply->display_name = "Multiply";
    multiply->category = "Math";
    multiply->header_file = "cler/blocks/math.hpp";
    
    PortSpec mul_in1;
    mul_in1.name = "in1";
    mul_in1.display_name = "Input 1";
    mul_in1.data_type = DataType::Float;
    mul_in1.cpp_type = "float";
    multiply->input_ports.push_back(mul_in1);
    
    PortSpec mul_in2;
    mul_in2.name = "in2";
    mul_in2.display_name = "Input 2";
    mul_in2.data_type = DataType::Float;
    mul_in2.cpp_type = "float";
    multiply->input_ports.push_back(mul_in2);
    
    PortSpec mul_out;
    mul_out.name = "out";
    mul_out.display_name = "Output";
    mul_out.data_type = DataType::Float;
    mul_out.cpp_type = "float";
    multiply->output_ports.push_back(mul_out);
    
    AddBlock(multiply);
}

void BlockLibrary::Draw(FlowCanvas* canvas)
{
    if (!canvas) return;
    
    ImGui::BeginChild("BlockList", ImVec2(0, 0), true);
    
#ifdef HAS_LIBCLANG
    // Controls for parsed blocks
    if (ImGui::Button("Load Desktop Blocks")) {
        StartLoadingDesktopBlocks();
        show_parsed_blocks = true;
        request_import_popup = true; // Just set the flag
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        RefreshLibrary();
        request_import_popup = true; // Also show popup for refresh
    }
    
    // Just show block count if we have parsed blocks
    if (show_parsed_blocks && !parsed_blocks.empty()) {
        ImGui::Text("Found %zu blocks", parsed_blocks.size());
    }
    ImGui::Separator();
#endif
    
    // Search filter
    static char searchBuffer[256] = {0};
    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();
    
    std::string search(searchBuffer);
    std::transform(search.begin(), search.end(), search.begin(), ::tolower);
    
    // Draw categories
    for (const auto& [category, blocks] : blocks_by_category) {
        if (blocks.empty()) continue;
        
        if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& block : blocks) {
                // Filter by search
                if (!search.empty()) {
                    std::string name = block->display_name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (name.find(search) == std::string::npos) {
                        continue;
                    }
                }
                
                // Draw block entry
                ImGui::PushID(block.get());
                
                bool selected = false;
                if (ImGui::Selectable(block->display_name.c_str(), &selected)) {
                    // Double-click to add
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        // Add at center of canvas view
                        ImVec2 canvas_center = ImGui::GetWindowPos();
                        canvas_center.x += ImGui::GetWindowWidth() / 2;
                        canvas_center.y += ImGui::GetWindowHeight() / 2;
                        canvas->AddNode(block, canvas_center);
                    }
                }
                
                // Drag to add
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("BLOCK_SPEC", &block, sizeof(block));
                    ImGui::Text("Add %s", block->display_name.c_str());
                    ImGui::EndDragDropSource();
                }
                
                // Tooltip with details
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", block->class_name.c_str());
                    if (!block->template_params.empty()) {
                        ImGui::Text("Template: ");
                        for (const auto& param : block->template_params) {
                            ImGui::Text("  %s = %s", 
                                       param.name.c_str(), param.default_value.c_str());
                        }
                    }
                    if (!block->constructor_params.empty()) {
                        ImGui::Text("Parameters:");
                        for (const auto& param : block->constructor_params) {
                            ImGui::Text("  %s: %s", param.name.c_str(), param.display_name.c_str());
                        }
                    }
                    ImGui::EndTooltip();
                }
                
                ImGui::PopID();
            }
        }
    }
    
    ImGui::EndChild();
}

} // namespace clerflow