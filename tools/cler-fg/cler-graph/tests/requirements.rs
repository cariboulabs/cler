use cler_graph::{block_requirements, extract_specs, required_block_origins, BlockRequirements};

fn palette() -> Vec<cler_graph::BlockSpec> {
    let udp = r#"
        template <typename T>
        struct SourceUdpBlock : cler::BlockBase {
            cler::Channel<T> in;
            SourceUdpBlock(const char* name) : BlockBase(name), in(4096) {}
            cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }
        };
        template <typename T>
        using UdpSource = SourceUdpBlock<T>;
    "#;
    let plot = r#"
        struct PlotBlock : cler::BlockBase {
            cler::Channel<float> in;
            PlotBlock(const char* name) : BlockBase(name), in(4096) {}
            cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }
        };
    "#;
    let mut specs = extract_specs(udp, "desktop_blocks/udp/source.hpp");
    specs.extend(extract_specs(plot, "desktop_blocks/plots/plot.hpp"));
    specs
}

#[test]
fn requirements_collect_known_types_anywhere_and_ignore_non_code() {
    let source = r#"
        // SourceUdpBlock<float> comment_only;
        const char* text = "PlotBlock string_only";

        namespace app {
        using LocalUdp = net::SourceUdpBlock<float>;

        struct Receiver : cler::BlockBase {
            LocalUdp input;
            UdpSource<float> backup;
            PlotBlock plot;
        };

        void unused() {
            net::SourceUdpBlock<float> disconnected("udp");
            auto another = PlotBlock("plot");
        }
        }
    "#;

    let requirements = block_requirements(source, &palette());
    assert_eq!(
        requirements.origins,
        vec![
            "desktop_blocks/plots/plot.hpp".to_string(),
            "desktop_blocks/udp/source.hpp".to_string(),
        ]
    );
    assert!(requirements.has_local_blocks);
    assert!(requirements.unknown_block_types.is_empty());
    assert!(!requirements.needs_fallback());
    assert_eq!(
        required_block_origins(source, &palette()),
        requirements.origins
    );
}

#[test]
fn requirements_report_local_and_unknown_block_types_for_safe_fallback() {
    let source = r#"
        struct MyBlock : cler::BlockBase {};
        struct PlainBlock {};

        void make() {
            MyBlock local;
            ThirdPartyBlock external;
            PlainBlock ordinary;
        }
    "#;

    assert_eq!(
        block_requirements(source, &[]),
        BlockRequirements {
            origins: Vec::new(),
            has_local_blocks: true,
            unknown_block_types: vec!["ThirdPartyBlock".to_string()],
            has_parse_errors: false,
        }
    );
}

#[test]
fn requirements_are_deduplicated_when_palette_names_overlap() {
    let mut specs = palette();
    let duplicate = specs[0].clone();
    specs.push(duplicate);
    let source = "UdpSource<float> second(\"two\");";

    assert_eq!(
        required_block_origins(source, &specs),
        vec!["desktop_blocks/udp/source.hpp".to_string()]
    );
}

#[test]
fn incomplete_editor_buffers_request_the_safe_fallback() {
    let requirements = block_requirements("SourceUdpBlock<float> source(", &palette());

    assert_eq!(
        requirements.origins,
        vec!["desktop_blocks/udp/source.hpp".to_string()]
    );
    assert!(requirements.has_parse_errors);
    assert!(requirements.needs_fallback());
}

#[test]
fn requirements_include_non_block_desktop_helpers() {
    let source = r#"
        #include "desktop_blocks/gui/gui_manager.hpp"
        #include <desktop_blocks/gui/system_helper.hpp>
        #include "unrelated/project_header.hpp"

        int main() {
            cler::GuiManager gui;
        }
    "#;

    assert_eq!(
        required_block_origins(source, &[]),
        vec![
            "desktop_blocks/gui/gui_manager.hpp".to_string(),
            "desktop_blocks/gui/system_helper.hpp".to_string(),
        ]
    );
}
