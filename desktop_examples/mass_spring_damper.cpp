#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <chrono>
#include <algorithm>
#include "desktop_blocks/gui/gui_manager.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/plots/plot_timeseries.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include <atomic>
#include <numbers>
#include <cmath>

const size_t SPS = 100; // Samples per second
const float DT = 1.0f / static_cast<float>(SPS);
const float wn = 1.0f; // Natural frequency
const float zeta = 0.5f; // Damping ratio
const float M = 1.0f; // Mass
const float K = wn * wn * M; // Spring constant
const float C = 2.0f * zeta * wn * M;

struct PlantBlock : public cler::BlockBase {
    cler::Channel<float> force_in;
    PlantBlock(const char* name)  
        : BlockBase(name), force_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {
            force_in.push(0.0f); //<-----MUST PROVIDE INITIAL FORCE OR CYCLIC GRAPH WILL FAIL
        } 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* measured_position_out) {
        if (force_in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (measured_position_out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transerable = std::min(force_in.size(), measured_position_out->space());

        for (size_t i = 0; i < transerable; ++i) {
            float force;
            force_in.pop(force);

            // Update position and velocity using the mass-spring-damper equations
            float acceleration = (force - K * _x - C * _v) / M;
            _v += acceleration * DT;
            _x += _v * DT + 0.5f * acceleration * DT * DT;
            measured_position_out->push(_x);
        }
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

        // Background and border
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(40, 40, 40, 255));
        draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 255));

        float spring_start_x = canvas_p0.x + 50.0f;
        float center_y = (canvas_p0.y + canvas_p1.y) * 0.5f;

        float spring_end_x = spring_start_x + 200.0f + _x * 20.0f;

        // Floor line
        draw_list->AddLine(
            ImVec2(canvas_p0.x + 20.0f, center_y + 50.0f),
            ImVec2(canvas_p1.x - 20.0f, center_y + 50.0f),
            IM_COL32(200, 200, 200, 60), 1.0f
        );

        // Fixed wall with hatch
        for (float y = center_y - 40.0f; y <= center_y + 40.0f; y += 8.0f) {
            draw_list->AddLine(
                ImVec2(spring_start_x - 20.0f, y),
                ImVec2(spring_start_x - 35.0f, y + 5.0f),
                IM_COL32(255, 255, 255, 150), 2.0f
            );
        }

        // Mass block
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

        // 🌀 Perfect sine spring: always ends at mass_center
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

        // Mass block shadow
        ImVec2 shadow_p0;
        shadow_p0.x = mass_p0.x + 4.0f;
        shadow_p0.y = mass_p0.y + 4.0f;
        ImVec2 shadow_p1;
        shadow_p1.x = mass_p1.x + 4.0f;
        shadow_p1.y = mass_p1.y + 4.0f;
        draw_list->AddRectFilled(shadow_p0, shadow_p1, IM_COL32(0, 0, 0, 100), 6.0f);

        // Mass block
        draw_list->AddRectFilled(mass_p0, mass_p1, IM_COL32(200, 50, 50, 255), 6.0f);
        draw_list->AddRect(mass_p0, mass_p1, IM_COL32(255, 255, 255, 180), 2.0f);

        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

    private:
        float _x = 0.0;
        float _v = 0.0;
        
        ImVec2 _initial_window_position {0.0f, 0.0f};
        ImVec2 _initial_window_size {600.0f, 300.0f};
};

struct ControllerBlock : public cler::BlockBase {
    cler::Channel<float> measured_position_in;

    ControllerBlock(const char* name)  
        : BlockBase(name), measured_position_in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {} 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* force_out) {
        if (measured_position_in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (force_out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transerable = std::min(measured_position_in.size(), force_out->space());
        for (size_t i = 0; i < transerable; ++i) {
            float measured_position;
            measured_position_in.pop(measured_position);

            // Atomically read parameters
            float target  = _target.load(std::memory_order_relaxed);
            float kp      = _kp.load(std::memory_order_relaxed);
            float ki      = _ki.load(std::memory_order_relaxed);
            float kd      = _kd.load(std::memory_order_relaxed);

            // Calculate error
            float ek = target - measured_position;

            // PID control
            float derivative = (ek - _ekm1) / DT; // Derivative term
            float dk = 0.95f * _dkm1 + 0.05f * derivative; // Low-pass filter for derivative term
            _int_state += ek * DT; // Integral term

            //Feed forward control
            float feed_forward = 0.0f;
            if (_feed_forward.load(std::memory_order_relaxed)) {
                feed_forward = K * target;
            }

            float force = kp * ek + ki * _int_state + kd * dk + feed_forward;

            _ekm1 = ek; // Update previous error
            _dkm1 = dk; // Update previous derivative

            force_out->push(force);
        }

        return cler::Empty{};
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("Controller");
        ImGui::Text("PID Controller");
        // Use a local copy for ImGui input, then store atomically
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
    float _ekm1 = 0.0;
    float _dkm1 = 0.0;
    float _int_state = 0.0;

    std::atomic<float> _target {10.0f};
    std::atomic<float> _kp {2.0f};
    std::atomic<float> _ki {1.0f};
    std::atomic<float> _kd {1.0f};
    std::atomic<bool>  _feed_forward {false};

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};

int main() {
    cler::GuiManager gui (1000, 600, "Mass-Spring-Damper Simulation");
    ControllerBlock controller("Controller");
    ThrottleBlock<float> throttle("Throttle", SPS);
    PlantBlock plant("Plant");


    FanoutBlock<float> fanout("Fanout", 2);

    PlotTimeSeriesBlock plot(
        "Sensor Plot",
        {"Measured Position"}, // signal labels
        SPS,
        100.0f // duration in seconds
    );

    controller.set_initial_window(0.0f, 0.0f, 175.0f, 200.0f);
    plot.set_initial_window(200.0f, 0.0f, 800.0f, 400.0f);
    plant.set_initial_window(200.0f, 400.0f, 800.0f, 200.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&controller, &throttle.in),
    cler::BlockRunner(&throttle, &plant.force_in),
    cler::BlockRunner(&plant, &fanout.in),
    cler::BlockRunner(&fanout, &plot.in[0], &controller.measured_position_in),
    cler::BlockRunner(&plot)
    );

    flowgraph.run();


    while (!gui.should_close()) {
        gui.begin_frame();
        plant.render();
        plot.render();
        controller.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}
