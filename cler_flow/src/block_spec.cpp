/******************************************************************************************
*                                                                                         *
*    BlockSpec - Implementation of block metadata                                        *
*                                                                                         *
******************************************************************************************/

#include "block_spec.hpp"
#include <sstream>
#include <algorithm>
#include <imgui.h>

namespace clerflow {

// Generate C++ instantiation code
std::string BlockSpec::generateInstantiation(const std::string& instance_name,
                                            const std::vector<std::string>& template_args,
                                            const std::vector<std::string>& constructor_args) const
{
    std::stringstream ss;
    
    // Generate the instantiation
    ss << "    auto " << instance_name << " = std::make_shared<" << class_name;
    
    // Add template arguments if any
    if (!template_args.empty()) {
        ss << "<";
        for (size_t i = 0; i < template_args.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << template_args[i];
        }
        ss << ">";
    }
    
    // Add constructor arguments
    ss << ">(";
    for (size_t i = 0; i < constructor_args.size(); ++i) {
        if (i > 0) ss << ", ";
        
        // Quote string parameters
        if (constructor_params[i].type == ParamType::String) {
            ss << "\"" << constructor_args[i] << "\"";
        } else {
            ss << constructor_args[i];
        }
    }
    ss << ");\n";
    
    return ss.str();
}

// Create from parsed C++ header (placeholder - will use tree-sitter or libclang later)
BlockSpec BlockSpec::fromHeader(const std::string& header_path)
{
    BlockSpec spec;
    spec.header_file = header_path;
    
    // TODO: Implement actual C++ parsing
    // For now, return empty spec
    
    return spec;
}

// Serialize to JSON
std::string BlockSpec::toJSON() const
{
    std::stringstream json;
    json << "{\n";
    json << "  \"class_name\": \"" << class_name << "\",\n";
    json << "  \"display_name\": \"" << display_name << "\",\n";
    json << "  \"category\": \"" << category << "\",\n";
    json << "  \"tooltip\": \"" << tooltip << "\",\n";
    json << "  \"header_file\": \"" << header_file << "\",\n";
    
    // Template parameters
    json << "  \"template_params\": [\n";
    for (size_t i = 0; i < template_params.size(); ++i) {
        const auto& param = template_params[i];
        json << "    {\n";
        json << "      \"name\": \"" << param.name << "\",\n";
        json << "      \"display_name\": \"" << param.display_name << "\",\n";
        json << "      \"type\": " << static_cast<int>(param.type) << ",\n";
        json << "      \"default_value\": \"" << param.default_value << "\",\n";
        json << "      \"tooltip\": \"" << param.tooltip << "\"\n";
        json << "    }";
        if (i < template_params.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ],\n";
    
    // Constructor parameters
    json << "  \"constructor_params\": [\n";
    for (size_t i = 0; i < constructor_params.size(); ++i) {
        const auto& param = constructor_params[i];
        json << "    {\n";
        json << "      \"name\": \"" << param.name << "\",\n";
        json << "      \"display_name\": \"" << param.display_name << "\",\n";
        json << "      \"type\": " << static_cast<int>(param.type) << ",\n";
        json << "      \"default_value\": \"" << param.default_value << "\",\n";
        json << "      \"tooltip\": \"" << param.tooltip << "\",\n";
        json << "      \"min\": " << param.min << ",\n";
        json << "      \"max\": " << param.max << "\n";
        json << "    }";
        if (i < constructor_params.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ],\n";
    
    // Input ports
    json << "  \"input_ports\": [\n";
    for (size_t i = 0; i < input_ports.size(); ++i) {
        const auto& port = input_ports[i];
        json << "    {\n";
        json << "      \"name\": \"" << port.name << "\",\n";
        json << "      \"display_name\": \"" << port.display_name << "\",\n";
        json << "      \"data_type\": " << static_cast<int>(port.data_type) << ",\n";
        json << "      \"cpp_type\": \"" << port.cpp_type << "\",\n";
        json << "      \"is_array\": " << (port.is_array ? "true" : "false") << ",\n";
        json << "      \"array_size\": " << port.array_size << "\n";
        json << "    }";
        if (i < input_ports.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ],\n";
    
    // Output ports
    json << "  \"output_ports\": [\n";
    for (size_t i = 0; i < output_ports.size(); ++i) {
        const auto& port = output_ports[i];
        json << "    {\n";
        json << "      \"name\": \"" << port.name << "\",\n";
        json << "      \"display_name\": \"" << port.display_name << "\",\n";
        json << "      \"data_type\": " << static_cast<int>(port.data_type) << ",\n";
        json << "      \"cpp_type\": \"" << port.cpp_type << "\"\n";
        json << "    }";
        if (i < output_ports.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ],\n";
    
    // Metadata
    json << "  \"is_source\": " << (is_source ? "true" : "false") << ",\n";
    json << "  \"is_sink\": " << (is_sink ? "true" : "false") << ",\n";
    json << "  \"is_hierarchical\": " << (is_hierarchical ? "true" : "false") << "\n";
    
    json << "}\n";
    
    return json.str();
}

// Deserialize from JSON (placeholder)
BlockSpec BlockSpec::fromJSON(const std::string& json)
{
    BlockSpec spec;
    
    // TODO: Implement proper JSON parsing
    // For now, return empty spec
    
    return spec;
}

// Helper function to get color for data type
uint32_t dataTypeToColor(DataType type)
{
    switch (type) {
        case DataType::Float:
            return IM_COL32(115, 140, 255, 255);  // Blue
        case DataType::Double:
            return IM_COL32(80, 120, 255, 255);   // Darker blue
        case DataType::Int:
            return IM_COL32(140, 255, 140, 255);  // Green
        case DataType::Bool:
            return IM_COL32(255, 140, 140, 255);  // Red
        case DataType::ComplexFloat:
            return IM_COL32(255, 180, 115, 255);  // Orange
        case DataType::ComplexDouble:
            return IM_COL32(255, 150, 80, 255);   // Darker orange
        case DataType::Custom:
            return IM_COL32(200, 200, 200, 255);  // Gray
        default:
            return IM_COL32(150, 150, 150, 255);  // Dark gray
    }
}

// Convert data type to string
std::string dataTypeToString(DataType type)
{
    switch (type) {
        case DataType::Float: return "float";
        case DataType::Double: return "double";
        case DataType::Int: return "int";
        case DataType::Bool: return "bool";
        case DataType::ComplexFloat: return "complex<float>";
        case DataType::ComplexDouble: return "complex<double>";
        case DataType::Custom: return "custom";
        default: return "unknown";
    }
}

// Convert string to data type
DataType stringToDataType(const std::string& str)
{
    if (str == "float") return DataType::Float;
    if (str == "double") return DataType::Double;
    if (str == "int") return DataType::Int;
    if (str == "bool") return DataType::Bool;
    if (str == "complex<float>") return DataType::ComplexFloat;
    if (str == "complex<double>") return DataType::ComplexDouble;
    return DataType::Custom;
}

} // namespace clerflow