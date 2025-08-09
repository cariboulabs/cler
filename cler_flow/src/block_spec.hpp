/******************************************************************************************
*                                                                                         *
*    BlockSpec - Modern block metadata specification                                     *
*                                                                                         *
*    Replaces CoreNode with a data-only specification                                    *
*                                                                                         *
******************************************************************************************/

#pragma once

#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace clerflow {

// Data types that can flow through connections
enum class DataType {
    Float,
    Double,
    Int,
    Bool,
    ComplexFloat,
    ComplexDouble,
    Custom
};

// Parameter types for constructor and template parameters
enum class ParamType {
    Int,
    Float,
    String,
    Bool,
    DataTypeSelector,
    Enum,
    FilePath
};

// Specification for a parameter (constructor or template)
struct ParamSpec {
    std::string name;
    std::string display_name;
    ParamType type;
    std::string default_value;
    std::string tooltip;
    
    // Range constraints for numeric types
    double min = 0.0;
    double max = 0.0;
    
    // Options for enum/selector types
    std::vector<std::string> options;
};

// Specification for a port (input or output)
struct PortSpec {
    std::string name;
    std::string display_name;
    DataType data_type;
    std::string cpp_type;  // Actual C++ type string
    
    // For input ports that are arrays
    bool is_array = false;
    int array_size = -1;  // -1 means dynamic
};

// Complete specification for a block
class BlockSpec {
public:
    // Identity
    std::string class_name;       // C++ class name
    std::string display_name;     // User-friendly name
    std::string category;         // For library organization (e.g., "Sources/Oscillators")
    std::string tooltip;          // Help text
    std::string header_file;      // Source .hpp file
    std::string library_name;     // Library name (e.g., "desktop_blocks")
    std::string library_path;     // Path to the library root directory
    
    // Parameters
    std::vector<ParamSpec> template_params;
    std::vector<ParamSpec> constructor_params;
    
    // Ports
    std::vector<PortSpec> input_ports;
    std::vector<PortSpec> output_ports;
    
    // Metadata
    bool is_source = false;       // Has no inputs
    bool is_sink = false;         // Has no outputs
    bool is_hierarchical = false; // Contains sub-flowgraph
    
    // Generate C++ instantiation code
    std::string generateInstantiation(const std::string& instance_name,
                                     const std::vector<std::string>& template_args,
                                     const std::vector<std::string>& constructor_args) const;
    
    // Create from parsed C++ header
    static BlockSpec fromHeader(const std::string& header_path);
    
    // Serialize/deserialize for save files
    std::string toJSON() const;
    static BlockSpec fromJSON(const std::string& json);
};

// Helper to get color for data type (for wire rendering)
uint32_t dataTypeToColor(DataType type);
std::string dataTypeToString(DataType type);
DataType stringToDataType(const std::string& str);

} // namespace clerflow