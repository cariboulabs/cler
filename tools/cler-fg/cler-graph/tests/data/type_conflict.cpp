#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/utils/throttle.hpp"
#include "desktop_blocks/utils/throughput.hpp"

#include <complex>

struct RampSourceBlock : public cler::BlockBase {
    RampSourceBlock(const char* name) : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        out->push(_next);
        _next += 1.0f;
        return cler::Empty{};
    }

  private:
    float _next = 0.0f;
};

struct ComplexSinkBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    ComplexSinkBlock(const char* name) : cler::BlockBase(name), in(1024) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        std::complex<float> sample;
        while (in.pop(sample)) {
        }
        return cler::Empty{};
    }
};

int main() {
    RampSourceBlock ramp("Ramp Source");
    ThrottleBlock<float> throttle("Throttle", 1000);
    ThroughputBlock<std::complex<float>> throughput("Throughput");
    ComplexSinkBlock sink("Complex Sink");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&ramp, &throttle.in),
        cler::BlockRunner(&throttle, &throughput.in),
        cler::BlockRunner(&throughput, &sink.in),
        cler::BlockRunner(&sink)
    );

    flowgraph.run();
    return 0;
}
