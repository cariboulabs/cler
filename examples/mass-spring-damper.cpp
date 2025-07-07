#include "cler.hpp"
#include <chrono>
#include <algorithm>
#include "gui_manager.hpp"
#include "blocks/throttle.hpp"
#include "blocks/plot_timeseries.hpp"
#include "blocks/fanout.hpp"

constexpr const size_t SPS = 100; // Samples per second
constexpr const float DT = 1.0f / static_cast<float>(SPS);
constexpr const float wn = 1.0f; // Natural frequency

constexpr const float zeta = 0.5f; // Damping ratio
constexpr const float M = 1.0f; // Mass
constexpr const float K = wn * wn * M; // Spring constant
constexpr const float C = 2.0f * zeta * wn * M;

struct PlantBlock : public cler::BlockBase {
    cler::Channel<float> force_in;
    PlantBlock(const char* name)  
        : BlockBase(name), force_in(cler::DEFAULT_BUFFER_SIZE) {
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

    private:
        float _x = 0.0;
        float _v = 0.0;
};

struct ControllerBlock : public cler::BlockBase {
    cler::Channel<float> measured_position_in;
    ControllerBlock(const char* name)  
        : BlockBase(name), measured_position_in(cler::DEFAULT_BUFFER_SIZE) {} 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::Channel<float>* force_out) {
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

            // Calculate error
            float ek = _target - measured_position;

            // PID control
            float derivative = (ek - _ekm1) / DT; // Derivative term
            float dk = 0.9f * _dkm1 + 0.1f * derivative; // Low-pass filter for derivative term
            _int_state += ek * DT; // Integral term

            // Calculate control force
            float force = _kp * ek + _ki * _int_state + _kd * dk;

            _ekm1 = ek; // Update previous error
            _dkm1 = dk; // Update previous derivative

            force_out->push(force);
        }

        return cler::Empty{};
    }

    //function to change the target position in dearimgui
    void render() {
        ImGui::Begin("Controller");  // You can name the window however you want

        ImGui::Text("PID Controller");
        ImGui::SliderFloat("Target Position", &_target, -10.0f, 10.0f);
        ImGui::InputFloat("Kp", &_kp, 0.1f, 1.0f);
        ImGui::InputFloat("Ki", &_ki, 0.1f, 1.0f);
        ImGui::InputFloat("Kd", &_kd, 0.1f, 1.0f);

        ImGui::End();
    }

    private:
        float _ekm1 = 0.0;
        float _dkm1 = 0.0;
        float _int_state = 0.0;
        
        float _target  = 10.0;
        float _kp = 2.0f;
        float _ki = 1.0f;
        float _kd = 1.0f;
};

int main() {
     cler::GuiManager gui (800, 600, "Mass-Spring-Damper Simulation");

    ControllerBlock controller("Controller");
    ThrottleBlock<float> throttle("Throttle", SPS);
    PlantBlock plant("Plant");

    FanoutBlock<float> fanout("Fanout", 2);

    const char* signal_labels[] = {"Position"};
    PlotTimeSeriesBlock plot(
        "Position Plot",
        1, // number of inputs
        signal_labels,
        SPS,
        100.0f // duration in seconds
    );

    cler::BlockRunner controller_runner(&controller, &throttle.in);
    cler::BlockRunner throttle_runner(&throttle, &plant.force_in);
    cler::BlockRunner plant_runner(&plant, &fanout.in);
    cler::BlockRunner fanout_runner(&fanout, &plot.in[0], &controller.measured_position_in);
    cler::BlockRunner plot_runner(&plot);

    cler::FlowGraph flowgraph(
    controller_runner,
    throttle_runner,
    plant_runner,
    fanout_runner,
    plot_runner
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        controller.render();
        plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    flowgraph.stop();
    return 0;
}
