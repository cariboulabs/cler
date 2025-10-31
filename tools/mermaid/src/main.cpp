#include "cpp_parser.h"
#include "mermaid_renderer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

struct ParseStats {
    int files_scanned = 0;
    int files_skipped = 0;
    int blocks_found = 0;
    int connections_found = 0;
    int files_succeeded = 0;
    int files_failed = 0;
    int warnings_total = 0;

    double success_rate() const {
        return files_scanned > 0 ?
            100.0 * files_succeeded / files_scanned : 0.0;
    }
};

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <input.cpp> [options]\n";
    std::cerr << "\nGenerates Mermaid flowchart visualization from Cler C++ flowgraph code.\n";
    std::cerr << "\nOptions:\n";
    std::cerr << "  -o <path>     Output file path (without .md extension)\n";
    std::cerr << "                Default: <input_filename>_flowgraph.md\n";
    std::cerr << "  -v, --verbose Show detailed parsing information\n";
    std::cerr << "  -h, --help    Show this help message\n";
    std::cerr << "\nExample:\n";
    std::cerr << "  " << prog_name << " example.cpp -o diagram\n";
    std::cerr << "  Creates: diagram.md\n";
}

std::string read_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::vector<std::string> input_files;
    std::string output_path;
    bool verbose = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            input_files.push_back(arg);
        } else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (input_files.empty()) {
        std::cerr << "Error: No input file specified\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (input_files.size() > 1 && !output_path.empty()) {
        std::cerr << "Error: Cannot specify -o with multiple input files\n";
        return 1;
    }

    ParseStats stats;
    cler::CppParser parser;
    cler::MermaidRenderer renderer;

    // Process each file with error recovery
    for (const auto& input_file : input_files) {
        stats.files_scanned++;

        try {
            // Read input file
            std::string content = read_file(input_file);

            // Fast pre-screen
            if (!cler::CppParser::is_flowgraph_file(content)) {
                if (verbose) {
                    std::cout << "⊘ " << input_file << " (no flowgraph detected)\n";
                }
                stats.files_skipped++;
                continue;
            }

            // Parse C++ file
            cler::FlowGraph flowgraph = parser.parse_file(content, input_file);

            if (!flowgraph.is_valid) {
                std::cerr << "✗ " << input_file << ": " << flowgraph.error_message << "\n";
                stats.files_failed++;
                continue;
            }

            // Check for blocks
            if (flowgraph.blocks.empty()) {
                std::cerr << "⚠ " << input_file << ": No blocks found\n";
                stats.files_failed++;
                continue;
            }

            // Determine output path
            std::string out_path = output_path;
            if (out_path.empty()) {
                size_t last_slash = input_file.find_last_of("/\\");
                size_t last_dot = input_file.find_last_of('.');
                std::string base_name;

                if (last_slash != std::string::npos) {
                    base_name = input_file.substr(last_slash + 1);
                } else {
                    base_name = input_file;
                }

                if (last_dot != std::string::npos && last_dot > last_slash) {
                    base_name = base_name.substr(0, last_dot - (last_slash == std::string::npos ? 0 : last_slash + 1));
                }

                out_path = base_name + "_flowgraph";
            }

            // Render to Mermaid
            renderer.render_to_file(flowgraph, out_path);

            // Report success
            if (verbose) {
                std::cout << "✓ " << input_file << "\n";
                std::cout << "  Blocks: " << flowgraph.blocks.size()
                         << ", Connections: " << flowgraph.connections.size() << "\n";
                if (!flowgraph.warnings.empty()) {
                    std::cout << "  Warnings: " << flowgraph.warnings.size() << "\n";
                    for (const auto& warning : flowgraph.warnings) {
                        std::cout << "    - " << warning << "\n";
                    }
                }
                std::cout << "  Output: " << out_path << ".md\n";
            } else {
                std::cout << "Generated: " << out_path << ".md\n";
            }

            stats.files_succeeded++;
            stats.blocks_found += flowgraph.blocks.size();
            stats.connections_found += flowgraph.connections.size();
            stats.warnings_total += flowgraph.warnings.size();

        } catch (const std::exception& e) {
            std::cerr << "✗ " << input_file << " (exception): " << e.what() << "\n";
            stats.files_failed++;
            // Continue with next file
        }
    }

    // Print statistics if verbose or multiple files
    if (verbose || input_files.size() > 1) {
        std::cout << "\n=== Summary ===\n";
        std::cout << "Files scanned: " << stats.files_scanned << "\n";
        std::cout << "Files skipped: " << stats.files_skipped << "\n";
        std::cout << "Succeeded: " << stats.files_succeeded << "\n";
        std::cout << "Failed: " << stats.files_failed << "\n";
        if (stats.files_scanned > 0) {
            std::cout << "Success rate: " << stats.success_rate() << "%\n";
        }
        if (verbose) {
            std::cout << "Total blocks: " << stats.blocks_found << "\n";
            std::cout << "Total connections: " << stats.connections_found << "\n";
            std::cout << "Total warnings: " << stats.warnings_total << "\n";
        }
    }

    return stats.files_failed > 0 ? 1 : 0;
}
