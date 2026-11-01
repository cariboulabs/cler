use cler_graph::extract_specs;
use cler_graph::palette_types::{BlockSpec, Direction, PortCount};

fn spec(source: &str, name: &str) -> BlockSpec {
    extract_specs(source, "<inline>")
        .into_iter()
        .find(|s| s.name == name)
        .unwrap_or_else(|| panic!("{name} not extracted"))
}

fn ports(spec: &BlockSpec, direction: Direction) -> Vec<String> {
    spec.ports
        .iter()
        .filter(|p| p.direction == direction)
        .map(|p| format!("{}{}", p.name, if p.variable { "[]" } else { "" }))
        .collect()
}

#[test]
fn b15_class_members_before_the_first_access_specifier_are_private() {
    let source = r#"
#include "cler.hpp"
template <typename T>
class ScratchBlock : public cler::BlockBase {
    cler::Channel<T> _scratch;
    cler::Channel<T> _spill;
public:
    cler::Channel<T> in;

    ScratchBlock(const char* name) : BlockBase(name), _scratch(64), _spill(64), in(64) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "ScratchBlock");
    assert_eq!(ports(&spec, Direction::Input), ["in"]);
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn b15b_struct_members_stay_public_by_default() {
    let source = r#"
#include "cler.hpp"
template <typename T>
struct ScratchBlock : public cler::BlockBase {
    cler::Channel<T> in;
    ScratchBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "ScratchBlock");
    assert_eq!(ports(&spec, Direction::Input), ["in"]);
    assert_eq!(spec.input_count, PortCount::Fixed(1));
}

#[test]
fn b16_preprocessor_guarded_field_degrades_the_whole_port_model() {
    let source = r#"
#include "cler.hpp"
struct DiagBlock : public cler::BlockBase {
    cler::Channel<float> in;
#ifdef CLER_WITH_DIAGNOSTICS
    cler::Channel<float> diag_in;
#endif
    DiagBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "DiagBlock");
    assert!(spec.conditional_members);
    assert_eq!(ports(&spec, Direction::Input), ["in"]);
    assert_eq!(spec.input_count, PortCount::Unknown);
    assert_eq!(spec.output_count, PortCount::Unknown);
}

#[test]
fn b16b_preprocessor_around_code_only_leaves_the_port_model_alone() {
    let source = r#"
#include "cler.hpp"
struct SimdBlock : public cler::BlockBase {
    cler::Channel<float> in;
    SimdBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t i = 0;
#if defined(__ARM_NEON)
        i += 4;
#endif
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "SimdBlock");
    assert!(!spec.conditional_members);
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn b17_two_variable_port_groups_in_one_direction_are_unknown() {
    let source = r#"
#include "cler.hpp"
template <typename T>
struct MixerBlock : public cler::BlockBase {
    cler::Channel<T>* sig_in = nullptr;
    cler::Channel<T>* ref_in = nullptr;

    MixerBlock(const char* name, size_t num_signals, size_t num_refs)
        : BlockBase(name), _num_signals(num_signals), _num_refs(num_refs) {
        sig_in = reinterpret_cast<cler::Channel<T>*>(_sig_storage);
        for (size_t i = 0; i < _num_signals; ++i) {
            new (&sig_in[i]) cler::Channel<T>(512);
        }
        ref_in = reinterpret_cast<cler::Channel<T>*>(_ref_storage);
        for (size_t i = 0; i < _num_refs; ++i) {
            new (&ref_in[i]) cler::Channel<T>(512);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }

    private:
        size_t _num_signals;
        size_t _num_refs;
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _sig_storage[8];
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _ref_storage[8];
};
"#;
    let spec = spec(source, "MixerBlock");
    assert_eq!(ports(&spec, Direction::Input), ["sig_in[]", "ref_in[]"]);
    assert_eq!(spec.input_count, PortCount::Unknown);
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn b17b_one_variable_group_still_resolves() {
    let source = r#"
#include "cler.hpp"
template <typename T>
struct MixerBlock : public cler::BlockBase {
    cler::Channel<T>* sig_in = nullptr;

    MixerBlock(const char* name, size_t num_signals)
        : BlockBase(name), _num_signals(num_signals) {
        sig_in = reinterpret_cast<cler::Channel<T>*>(_sig_storage);
        for (size_t i = 0; i < _num_signals; ++i) {
            new (&sig_in[i]) cler::Channel<T>(512);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }

    private:
        size_t _num_signals;
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _sig_storage[8];
};
"#;
    let spec = spec(source, "MixerBlock");
    assert_eq!(spec.input_count, PortCount::CtorArg(1));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn b18_bound_from_a_two_arg_expression_degrades_to_unknown() {
    let source = r#"
#include "cler.hpp"
template <typename T>
struct GridBlock : public cler::BlockBase {
    cler::Channel<T>* in = nullptr;

    GridBlock(const char* name, size_t rows, size_t cols)
        : BlockBase(name), _cells(rows * cols) {
        in = reinterpret_cast<cler::Channel<T>*>(_storage);
        for (size_t i = 0; i < _cells; ++i) {
            new (&in[i]) cler::Channel<T>(512);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }

    private:
        size_t _cells;
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _storage[64];
};
"#;
    let spec = spec(source, "GridBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
}

#[test]
fn b18b_bound_assigned_in_the_ctor_body_degrades_to_unknown() {
    let source = r#"
#include "cler.hpp"
template <typename T>
struct GridBlock : public cler::BlockBase {
    cler::Channel<T>* in = nullptr;

    GridBlock(const char* name, size_t rows, size_t cols) : BlockBase(name) {
        _cells = rows * cols;
        in = reinterpret_cast<cler::Channel<T>*>(_storage);
        for (size_t i = 0; i < _cells; ++i) {
            new (&in[i]) cler::Channel<T>(512);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }

    private:
        size_t _cells;
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _storage[64];
};
"#;
    let spec = spec(source, "GridBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
}

#[test]
fn b19_channel_array_member_is_a_port_array_not_a_scalar() {
    let source = r#"
#include "cler.hpp"
struct QuadBlock : public cler::BlockBase {
    cler::Channel<float, 512> in[4];
    QuadBlock(const char* name) : BlockBase(name) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "QuadBlock");
    assert_eq!(ports(&spec, Direction::Input), ["in[]"]);
    assert_eq!(spec.input_count, PortCount::Fixed(4));
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}

#[test]
fn b19b_channel_array_sized_by_a_template_param_is_a_template_arg() {
    let source = r#"
#include "cler.hpp"
template <typename T, size_t N>
struct QuadBlock : public cler::BlockBase {
    cler::Channel<T> in[N];
    QuadBlock(const char* name) : BlockBase(name) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "QuadBlock");
    assert_eq!(ports(&spec, Direction::Input), ["in[]"]);
    assert_eq!(spec.input_count, PortCount::TemplateArg(1));
}

#[test]
fn b19c_channel_array_sized_by_an_unknown_symbol_degrades() {
    let source = r#"
#include "cler.hpp"
struct QuadBlock : public cler::BlockBase {
    cler::Channel<float> in[MAX_SLOTS];
    QuadBlock(const char* name) : BlockBase(name) {}
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        return cler::Empty{};
    }
};
"#;
    let spec = spec(source, "QuadBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
}

#[test]
fn b20_disagreeing_overloaded_ctors_degrade_to_unknown() {
    let source = r#"
#include "cler.hpp"
struct LabeledPlotBlock : public cler::BlockBase {
    cler::Channel<float>* in = nullptr;

    LabeledPlotBlock(const char* name, size_t num_signals)
        : BlockBase(name), _n(num_signals) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    LabeledPlotBlock(const char* name, std::vector<std::string> labels, size_t sps)
        : BlockBase(name), _n(labels.size()) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }

    private:
        size_t _n;
        std::aligned_storage_t<sizeof(cler::Channel<float>), alignof(cler::Channel<float>)> _storage[16];
};
"#;
    let spec = spec(source, "LabeledPlotBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
}

#[test]
fn b20b_agreeing_overloaded_ctors_keep_the_authority() {
    let source = r#"
#include "cler.hpp"
struct LabeledPlotBlock : public cler::BlockBase {
    cler::Channel<float>* in = nullptr;

    LabeledPlotBlock(const char* name, size_t num_signals)
        : BlockBase(name), _n(num_signals) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    LabeledPlotBlock(const char* name, size_t num_signals, float sps)
        : BlockBase(name), _n(num_signals) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }

    private:
        size_t _n;
        std::aligned_storage_t<sizeof(cler::Channel<float>), alignof(cler::Channel<float>)> _storage[16];
};
"#;
    let spec = spec(source, "LabeledPlotBlock");
    assert_eq!(spec.input_count, PortCount::CtorArg(1));
}

#[test]
fn b20c_the_same_authority_at_a_different_index_degrades_to_unknown() {
    let source = r#"
#include "cler.hpp"
struct LabeledPlotBlock : public cler::BlockBase {
    cler::Channel<float>* in = nullptr;

    LabeledPlotBlock(const char* name, size_t num_signals)
        : BlockBase(name), _n(num_signals) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    LabeledPlotBlock(const char* name, float sps, size_t num_signals)
        : BlockBase(name), _n(num_signals) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }

    private:
        size_t _n;
        std::aligned_storage_t<sizeof(cler::Channel<float>), alignof(cler::Channel<float>)> _storage[16];
};
"#;
    let spec = spec(source, "LabeledPlotBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
}

#[test]
fn b20d_deleted_copy_and_move_ctors_are_not_overloads() {
    let source = r#"
#include "cler.hpp"
struct LabeledPlotBlock : public cler::BlockBase {
    cler::Channel<float>* in = nullptr;

    LabeledPlotBlock(const char* name, size_t num_signals)
        : BlockBase(name), _n(num_signals) {
        in = reinterpret_cast<cler::Channel<float>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&in[i]) cler::Channel<float>(512); }
    }

    LabeledPlotBlock(const LabeledPlotBlock&) = delete;
    LabeledPlotBlock(LabeledPlotBlock&& other) : BlockBase("moved") {}

    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }

    private:
        size_t _n;
        std::aligned_storage_t<sizeof(cler::Channel<float>), alignof(cler::Channel<float>)> _storage[16];
};
"#;
    let spec = spec(source, "LabeledPlotBlock");
    assert_eq!(spec.input_count, PortCount::CtorArg(1));
    assert_eq!(spec.ctor_params.len(), 2);
    assert_eq!(spec.ctor_params[1].name, "num_signals");
}

#[test]
fn b21_may_block_is_read_from_the_declaration_not_a_grep() {
    let tight = r#"
#include "cler.hpp"
struct TightBlock : public cler::BlockBase {
    static constexpr bool may_block=true;
    cler::Channel<float> in;
    TightBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }
};
"#;
    let wrapped = r#"
#include "cler.hpp"
struct WrappedBlock : public cler::BlockBase {
    static constexpr bool
        may_block = true;
    cler::Channel<float> in;
    WrappedBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure() { return cler::Empty{}; }
};
"#;
    assert!(spec(tight, "TightBlock").may_block);
    assert!(spec(wrapped, "WrappedBlock").may_block);
}

#[test]
fn b21b_may_block_false_and_a_mention_in_code_stay_false() {
    let source = r#"
#include "cler.hpp"
struct CalmBlock : public cler::BlockBase {
    static constexpr bool may_block = false;
    cler::Channel<float> in;
    CalmBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure() {
        bool would_block = CalmBlock::may_block;
        return cler::Empty{};
    }
};
"#;
    assert!(!spec(source, "CalmBlock").may_block);
}

#[test]
fn b22_counts_are_per_direction() {
    let source = r#"
#include "cler.hpp"
struct SplitBlock : public cler::BlockBase {
    cler::Channel<float> in;
    SplitBlock(const char* name) : BlockBase(name), in(64) {}
    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* a_out,
        cler::ChannelBase<float>* b_out,
        cler::ChannelBase<float>* c_out) { return cler::Empty{}; }
};
"#;
    let spec = spec(source, "SplitBlock");
    assert_eq!(spec.inputs().count(), 1);
    assert_eq!(spec.outputs().count(), 3);
    assert_eq!(spec.input_count, PortCount::Fixed(1));
    assert_eq!(spec.output_count, PortCount::Fixed(3));
}

#[test]
fn b22b_a_variable_group_and_a_scalar_in_one_direction_degrade() {
    let source = r#"
#include "cler.hpp"
template <typename T>
struct MixedBlock : public cler::BlockBase {
    cler::Channel<T> ref_in;
    cler::Channel<T>* sig_in = nullptr;

    MixedBlock(const char* name, size_t num_signals)
        : BlockBase(name), ref_in(64), _n(num_signals) {
        sig_in = reinterpret_cast<cler::Channel<T>*>(_storage);
        for (size_t i = 0; i < _n; ++i) { new (&sig_in[i]) cler::Channel<T>(512); }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        return cler::Empty{};
    }

    private:
        size_t _n;
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _storage[8];
};
"#;
    let spec = spec(source, "MixedBlock");
    assert_eq!(spec.input_count, PortCount::Unknown);
    assert_eq!(spec.output_count, PortCount::Fixed(1));
}
