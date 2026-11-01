#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include "desktop_blocks/sigmf/source_sigmf.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

static void usage() {
    std::cout << "usage:\n"
              << "  sigmf_tools info <base>\n"
              << "  sigmf_tools from-cs8 <in.cs8> <base> --rate <hz> --freq <hz> [--datatype ci8]\n"
              << "  sigmf_tools play <base>\n";
}

static int info(const char* base) {
    sigmf::Meta meta = sigmf::read_meta(base);
    std::cout << sigmf::to_json(meta);
    return 0;
}

static int from_cs8(int argc, char** argv) {
    if (argc < 4) { usage(); return 1; }
    const char* input = argv[2];
    const char* base = argv[3];
    sigmf::Meta meta;
    meta.datatype = sigmf::Datatype::ci8;
    sigmf::Capture capture;
    capture.has_frequency = true;
    capture.datetime = sigmf::utc_now();

    for (int i = 4; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--rate") == 0) meta.sample_rate = std::atof(argv[i + 1]);
        else if (std::strcmp(argv[i], "--freq") == 0) capture.frequency = std::atof(argv[i + 1]);
        else if (std::strcmp(argv[i], "--datatype") == 0) meta.datatype = sigmf::parse_datatype(argv[i + 1]);
        else { usage(); return 1; }
    }
    meta.captures.push_back(capture);

    FILE* in = std::fopen(input, "rb");
    if (!in) { std::cerr << "cannot open " << input << "\n"; return 1; }
    std::string out_path = sigmf::data_path(base);
    FILE* out = std::fopen(out_path.c_str(), "wb");
    if (!out) { std::fclose(in); std::cerr << "cannot write " << out_path << "\n"; return 1; }
    char buf[65536];
    size_t n;
    size_t bytes = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
        std::fwrite(buf, 1, n, out);
        bytes += n;
    }
    std::fclose(in);
    std::fclose(out);
    if (!sigmf::write_meta(base, meta)) { std::cerr << "cannot write metadata\n"; return 1; }
    std::cout << "wrote " << sigmf::meta_path(base) << " and " << out_path << " ("
              << bytes / sigmf::datatype_size(meta.datatype) << " samples)\n";
    return 0;
}

static int play(const char* base) {
    SourceSigMFBlock<std::complex<float>> source("SigMFSource", base);
    SinkNullBlock<std::complex<float>> sink("Null");
    std::cout << "datatype " << sigmf::datatype_name(source.datatype())
              << ", rate " << source.sample_rate() << " Hz"
              << ", frequency " << source.center_frequency() << " Hz\n";

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );
    flowgraph.run();
    while (!flowgraph.is_stopped()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    flowgraph.stop();
    std::cout << "streamed " << sink.in.consumer_thread_cumulative_read_count() << " samples\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string command = argv[1];
    if (command == "info") return info(argv[2]);
    if (command == "from-cs8") return from_cs8(argc, argv);
    if (command == "play") return play(argv[2]);
    usage();
    return 1;
}
