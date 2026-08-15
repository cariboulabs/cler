#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <algorithm>
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include <atomic>
#include <cmath>
#include <complex>

constexpr size_t SPS = 100;
constexpr float DT = 1.0f / static_cast<float>(SPS);
constexpr float wn = 1.0f;
constexpr float zeta = 0.5f;
constexpr float M = 1.0f;
constexpr float K = wn * wn * M;
constexpr float C = 2.0f * zeta * wn * M;
constexpr float DERIVATIVE_TAU = 0.05f;

struct PlantBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<float> force_in;
    PlantBlock(const char* name)  
        : BlockBase(name), force_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {
            force_in.push(0.0f);
        }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* measured_position_out) {
        auto [read_ptr, read_size] = force_in.read_dbf();
        auto [write_ptr, write_size] = measured_position_out->write_dbf();

        size_t to_process = std::min(read_size, write_size);
        if (to_process == 0 || !read_ptr || !write_ptr) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        float x = _x.load(std::memory_order_relaxed);
        for (size_t i = 0; i < to_process; ++i) {
            float force = read_ptr[i];

            float acceleration = (force - K * x - C * _v) / M;
            _v += acceleration * DT;
            x += _v * DT;
            write_ptr[i] = x;
        }
        _x.store(x, std::memory_order_relaxed);
        force_in.commit_read(to_process);
        measured_position_out->commit_write(to_process);
        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Plant");

        ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
        if (canvas_sz.x < 200.0f) canvas_sz.x = 200.0f;
        if (canvas_sz.y < 100.0f) canvas_sz.y = 100.0f;
        ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(40, 40, 40, 255));
        draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255));

        float spring_start_x = canvas_p0.x + 50.0f;
        float center_y = (canvas_p0.y + canvas_p1.y) * 0.5f;

        float spring_end_x = spring_start_x + 200.0f + _x.load(std::memory_order_relaxed) * 20.0f;

        draw_list->AddLine(
            ImVec2(canvas_p0.x + 20.0f, center_y + 50.0f),
            ImVec2(canvas_p1.x - 20.0f, center_y + 50.0f),
            IM_COL32(200, 200, 200, 60), 1.0f
        );

        for (float y = center_y - 40.0f; y <= center_y + 40.0f; y += 8.0f) {
            draw_list->AddLine(
                ImVec2(spring_start_x - 20.0f, y),
                ImVec2(spring_start_x - 35.0f, y + 5.0f),
                IM_COL32(255, 255, 255, 150), 2.0f
            );
        }

        ImVec2 mass_size = ImVec2(40.0f, 40.0f);

        ImVec2 mass_p0;
        mass_p0.x = spring_end_x;
        mass_p0.y = center_y - mass_size.y * 0.5f;

        ImVec2 mass_p1;
        mass_p1.x = spring_end_x + mass_size.x;
        mass_p1.y = center_y + mass_size.y * 0.5f;

        ImVec2 mass_center;
        mass_center.x = (mass_p0.x + mass_p1.x) * 0.5f;
        mass_center.y = (mass_p0.y + mass_p1.y) * 0.5f;

        // Spring coil geometry is generated so it always ends at mass_center
        int num_points = 100;
        float coil_length = mass_center.x - 0.5f*(mass_size.x) - spring_start_x;
        float amplitude = 12.0f;
        float cycles = 5.0f; // number of full waves between start and end

        ImVec2 prev;
        prev.x = spring_start_x;
        prev.y = center_y;

        for (int i = 1; i <= num_points; ++i) {
            float t = (float)i / (float)num_points;
            float x = spring_start_x + t * coil_length;
            float phase = t * cycles * 2.0f * cler::PI;
            float y = center_y + sinf(phase) * amplitude;

            draw_list->AddLine(prev, ImVec2(x, y), IM_COL32(255, 215, 0, 255), 3.0f);
            prev.x = x;
            prev.y = y;
        }

        ImVec2 shadow_p0;
        shadow_p0.x = mass_p0.x + 4.0f;
        shadow_p0.y = mass_p0.y + 4.0f;
        ImVec2 shadow_p1;
        shadow_p1.x = mass_p1.x + 4.0f;
        shadow_p1.y = mass_p1.y + 4.0f;
        draw_list->AddRectFilled(shadow_p0, shadow_p1, IM_COL32(0, 0, 0, 100), 6.0f);

        draw_list->AddRectFilled(mass_p0, mass_p1, IM_COL32(200, 50, 50, 255), 6.0f);
        draw_list->AddRect(mass_p0, mass_p1, IM_COL32(255, 255, 255, 180), 2.0f);

        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

    private:
        std::atomic<float> _x {0.0f};
        float _v = 0.0f;
        
        ImVec2 _initial_window_position {0.0f, 0.0f};
        ImVec2 _initial_window_size {600.0f, 300.0f};
};

struct ControllerBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<float> measured_position_in;
    float* _buffer;
    float* _error_buffer;
    size_t _buffer_size;

    ControllerBlock(const char* name)
        : BlockBase(name), measured_position_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {
        _buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float);
        _buffer = new float[_buffer_size];
        _error_buffer = new float[_buffer_size];
    }

    ~ControllerBlock() {
        delete[] _buffer;
        delete[] _error_buffer;
    }

    float kp() const { return _kp.load(std::memory_order_relaxed); }
    float ki() const { return _ki.load(std::memory_order_relaxed); }
    float kd() const { return _kd.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* force_out, cler::ChannelBase<float>* error_out) {
        size_t transferable = std::min(
            {measured_position_in.size(), force_out->space(), error_out->space(), _buffer_size});
        if (transferable == 0) return cler::Error::NotEnoughSpaceOrSamples;

        measured_position_in.readN(_buffer, transferable);

        float target  = _target.load(std::memory_order_relaxed);
        float kp      = _kp.load(std::memory_order_relaxed);
        float ki      = _ki.load(std::memory_order_relaxed);
        float kd      = _kd.load(std::memory_order_relaxed);
        bool feed_forward_enabled = _feed_forward.load(std::memory_order_relaxed);

        constexpr float ALPHA = DERIVATIVE_TAU / (DERIVATIVE_TAU + DT);
        constexpr float INTEGRAL_LIMIT = 20.0f;

        for (size_t i = 0; i < transferable; ++i) {
            float measured_position = _buffer[i];
            float ek = target - measured_position;

            // Derivative of the measurement, not the error: a target step
            // must not kick the D term.
            float derivative = -(measured_position - _xkm1) / DT;
            float dk = ALPHA * _dkm1 + (1.0f - ALPHA) * derivative;
            _int_state += ek * DT;
            _int_state = std::clamp(_int_state, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);

            float feed_forward = 0.0f;
            if (feed_forward_enabled) {
                feed_forward = K * target;
            }

            float force = kp * ek + ki * _int_state + kd * dk + feed_forward;

            _xkm1 = measured_position;
            _dkm1 = dk;

            _buffer[i] = force;
            _error_buffer[i] = ek;
        }

        force_out->writeN(_buffer, transferable);
        error_out->writeN(_error_buffer, transferable);

        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Controller");
        ImGui::Text("PID Controller");
        float tmp_target = _target.load();
        if (ImGui::SliderFloat("Target", &tmp_target, -10.0f, 10.0f)) {
            _target.store(tmp_target);
        }

        float tmp_kp = _kp.load();
        if (ImGui::InputFloat("Kp", &tmp_kp, 0.1f, 1.0f)) {
            _kp.store(tmp_kp);
        }

        float tmp_ki = _ki.load();
        if (ImGui::InputFloat("Ki", &tmp_ki, 0.1f, 1.0f)) {
            _ki.store(tmp_ki);
        }

        float tmp_kd = _kd.load();
        if (ImGui::InputFloat("Kd", &tmp_kd, 0.1f, 1.0f)) {
            _kd.store(tmp_kd);
        }

        bool feed_forward = _feed_forward.load();
        ImGui::Checkbox("Feed Forward", &feed_forward);
        if (feed_forward) {
            _feed_forward.store(true);
        } else {
            _feed_forward.store(false);
        }

        ImGui::End();
    }


    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    float _xkm1 = 0.0f;
    float _dkm1 = 0.0f;
    float _int_state = 0.0f;

    std::atomic<float> _target {10.0f};
    std::atomic<float> _kp {99.0f};
    std::atomic<float> _ki {5.0f};
    std::atomic<float> _kd {19.0f};
    std::atomic<bool>  _feed_forward {true};

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};

struct RootLocusBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    RootLocusBlock(const char* name, const ControllerBlock* controller)
        : BlockBase(name), _controller(controller) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        return cler::Error::NotEnoughSamples;
    }

    void render() {
        float kp = _controller->kp();
        float ki = _controller->ki();
        float kd = _controller->kd();
        if (kp != _kp || ki != _ki || kd != _kd) {
            _kp = kp;
            _ki = ki;
            _kd = kd;
            sweep();
        }

        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Root Locus");
        ImGui::Text("Closed-loop poles as loop gain sweeps 0..2x (squares: 1x)");
        if (ImPlot::BeginPlot("##locus", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Re [rad/s]", "Im [rad/s]");
            float zero = 0.0f;
            ImPlot::PlotInfLines("##stability", &zero, 1,
                                 {ImPlotProp_LineColor, ImVec4(0.7f, 0.2f, 0.2f, 0.8f)});
            ImPlot::PlotScatter("locus", _re, _im, GAIN_STEPS * ORDER,
                                {ImPlotProp_Marker, ImPlotMarker_Circle,
                                 ImPlotProp_MarkerSize, 2.0f});
            ImPlot::PlotScatter("current gains", _nom_re, _nom_im, ORDER,
                                {ImPlotProp_Marker, ImPlotMarker_Square,
                                 ImPlotProp_MarkerSize, 5.0f});
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    static constexpr int GAIN_STEPS = 60;
    static constexpr int ORDER = 4;

    // Characteristic polynomial of the loop with the filtered PID:
    //   s(tau s+1)(M s^2 + C s + K) + g [ (kp tau + kd) s^2 + (kp + ki tau) s + ki ] = 0
    void sweep() {
        constexpr double tau = DERIVATIVE_TAU;
        for (int step = 1; step <= GAIN_STEPS; ++step) {
            double g = 2.0 * step / GAIN_STEPS;
            double a[ORDER + 1] = {
                g * _ki,
                K + g * (_kp + _ki * tau),
                tau * K + C + g * (_kp * tau + _kd),
                tau * C + M,
                tau * M,
            };
            std::complex<double> roots[ORDER];
            solve_quartic(a, roots);
            for (int r = 0; r < ORDER; ++r) {
                size_t at = static_cast<size_t>(step - 1) * ORDER + r;
                _re[at] = static_cast<float>(roots[r].real());
                _im[at] = static_cast<float>(roots[r].imag());
                if (step == GAIN_STEPS / 2) {
                    _nom_re[r] = _re[at];
                    _nom_im[r] = _im[at];
                }
            }
        }
    }

    static void solve_quartic(const double a[ORDER + 1], std::complex<double>* roots) {
        std::complex<double> monic[ORDER];
        for (int i = 0; i < ORDER; ++i) monic[i] = a[i] / a[ORDER];
        std::complex<double> seed(0.4, 0.9);
        for (int i = 0; i < ORDER; ++i) roots[i] = std::pow(seed, i + 1);
        for (int pass = 0; pass < 80; ++pass) {
            for (int i = 0; i < ORDER; ++i) {
                std::complex<double> value = 1.0;
                for (int p = ORDER - 1; p >= 0; --p) value = value * roots[i] + monic[p];
                std::complex<double> denom = 1.0;
                for (int j = 0; j < ORDER; ++j) {
                    if (j != i) denom *= roots[i] - roots[j];
                }
                roots[i] -= value / denom;
            }
        }
    }

    const ControllerBlock* _controller;
    float _kp = -1.0f;
    float _ki = -1.0f;
    float _kd = -1.0f;
    float _re[GAIN_STEPS * ORDER] = {};
    float _im[GAIN_STEPS * ORDER] = {};
    float _nom_re[ORDER] = {};
    float _nom_im[ORDER] = {};

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 400.0f};
};

int main() {
    const float GW = 1400.0f;
    const float GH = 800.0f;
    cler::GuiManager gui (static_cast<size_t>(GW), static_cast<size_t>(GH), "Mass-Spring-Damper Simulation");
    ControllerBlock controller("Controller");
    ThrottleBlock<float> throttle("Throttle", SPS);
    PlantBlock plant("Plant");
    RootLocusBlock root_locus("RootLocus", &controller);

    FanoutBlock<float> fanout("Fanout", 2);

    PlotTimeSeriesBlock plot(
        "Sensor Plot",
        {"Measured Position"},
        SPS,
        100.0f
    );

    PlotTimeSeriesBlock error_plot(
        "Error Plot",
        {"target - x"},
        SPS,
        100.0f
    );

    plant.set_initial_window(0.0f, 0.0f, GW, 220.0f);
    controller.set_initial_window(0.0f, 220.0f, 260.0f, GH - 220.0f);
    plot.set_initial_window(260.0f, 220.0f, 570.0f, (GH - 220.0f) / 2.0f);
    error_plot.set_initial_window(260.0f, 220.0f + (GH - 220.0f) / 2.0f, 570.0f, (GH - 220.0f) / 2.0f);
    root_locus.set_initial_window(830.0f, 220.0f, GW - 830.0f, GH - 220.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&controller, &throttle.in, &error_plot.in[0]),
    cler::BlockRunner(&throttle, &plant.force_in),
    cler::BlockRunner(&plant, &fanout.in),
    cler::BlockRunner(&fanout, &plot.in[0], &controller.measured_position_in),
    cler::BlockRunner(&plot),
    cler::BlockRunner(&error_plot),
    cler::BlockRunner(&root_locus)
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.render(flowgraph);
    }
    flowgraph.stop();
    cler::print_flowgraph_execution_report(flowgraph);
    return 0;
}
