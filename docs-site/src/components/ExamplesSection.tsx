'use client';

import { useState } from 'react';
import { Play, Radio, Activity, Waves, ExternalLink } from 'lucide-react';
import CodeEditor from './CodeEditor';

const examples = [
  {
    id: 'physics',
    title: 'Mass-Spring-Damper',
    description: 'Physics simulation demonstrating CLER\'s capabilities beyond radio',
    icon: Activity,
    complexity: 'Beginner',
    tags: ['Physics', 'Simulation', 'Real-time'],
    code: `// Mass-Spring-Damper Physics Simulation
int main() {
    cler::GuiManager gui(1200, 800, "Mass Spring Damper Simulation");
    
    // Physical parameters
    const float mass = 1.0f;
    const float spring_k = 50.0f;
    const float damping_c = 2.0f;
    const float dt = 0.001f; // 1ms timestep
    
    // Create simulation blocks
    MassSpringDamperBlock simulator("Physics", mass, spring_k, damping_c, dt);
    ThrottleBlock<float> throttle("Throttle", 1000); // 1000 Hz
    
    // Create visualization
    PlotTimeSeriesBlock position_plot("Position", {"Position (m)"}, 1000, 10.0f);
    PlotTimeSeriesBlock velocity_plot("Velocity", {"Velocity (m/s)"}, 1000, 10.0f);
    
    // Connect the flowgraph
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&simulator, &throttle.in),
        cler::BlockRunner(&throttle, &position_plot.in[0]),
        cler::BlockRunner(&position_plot)
    );
    
    flowgraph.run();
    
    // Interactive simulation
    while (!gui.should_close()) {
        gui.begin_frame();
        
        // Apply external forces with mouse
        if (gui.is_mouse_clicked()) {
            auto mouse_pos = gui.get_mouse_position();
            simulator.apply_external_force(mouse_pos.x / 100.0f);
        }
        
        position_plot.render();
        velocity_plot.render();
        gui.end_frame();
    }
    
    return 0;
}`,
    features: [
      'Real-time physics simulation',
      'Interactive mouse control',
      'Multiple plot visualizations',
      'Demonstrates non-radio use cases'
    ],
    output: 'Interactive physics simulation with real-time plotting'
  },
  {
    id: 'sdr',
    title: 'SDR Receiver',
    description: 'Software Defined Radio receiver with real hardware',
    icon: Radio,
    complexity: 'Intermediate',
    tags: ['SDR', 'Hardware', 'RF'],
    code: `// HackRF SDR Receiver Example
int main() {
    cler::GuiManager gui(1200, 800, "SDR Receiver");
    
    // SDR Hardware source
    const double center_freq = 100.1e6; // 100.1 MHz FM station
    const double sample_rate = 2e6;     // 2 MHz bandwidth
    
    SourceHackRFBlock source("HackRF", center_freq, sample_rate);
    
    // RF processing chain
    PolyphaseChannelizerBlock<std::complex<float>> channelizer(
        "Channelizer", 64, 0.4f
    );
    
    // Demodulation
    FMDemodBlock demod("FM Demod", sample_rate);
    
    // Audio processing
    ResamplerBlock<float> resampler("Resampler", sample_rate, 48000);
    AudioSinkBlock audio_sink("Audio", 48000);
    
    // Visualization
    PlotCSpectrumBlock spectrum_plot("Spectrum", 1024, sample_rate);
    PlotCSpectrogramBlock waterfall("Waterfall", 1024, sample_rate);
    
    // Connect the flowgraph
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &channelizer.in),
        cler::BlockRunner(&channelizer, &demod.in, &spectrum_plot.in[0]),
        cler::BlockRunner(&demod, &resampler.in),
        cler::BlockRunner(&resampler, &audio_sink.in),
        cler::BlockRunner(&spectrum_plot),
        cler::BlockRunner(&waterfall)
    );
    
    flowgraph.run();
    
    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum_plot.render();
        waterfall.render();
        gui.end_frame();
    }
    
    return 0;
}`,
    features: [
      'Real SDR hardware integration',
      'Polyphase channelization',
      'FM demodulation',
      'Real-time spectrum analysis',
      'Audio output'
    ],
    output: 'Live FM radio reception with spectrum visualization'
  },
  {
    id: 'gmsk',
    title: 'GMSK Demodulator',
    description: 'Advanced digital signal processing for wireless protocols',
    icon: Waves,
    complexity: 'Advanced',
    tags: ['DSP', 'Demodulation', 'Wireless'],
    code: `// GMSK Demodulator for Wireless Protocols
int main() {
    cler::GuiManager gui(1200, 800, "GMSK Demodulator");
    
    // Load recorded IQ samples
    SourceFileBlock<std::complex<float>> source("Input", "recorded_signal.bin");
    
    // Preprocessing
    const size_t decimation = 8;
    const float symbol_rate = 38400.0f;
    const float sample_rate = symbol_rate * decimation;
    
    MultistageResamplerBlock<std::complex<float>> resampler(
        "Resampler", sample_rate, symbol_rate * 2
    );
    
    // GMSK demodulation
    EZGMSKDemodBlock demod("GMSK Demod", symbol_rate);
    
    // Packet processing
    PacketSyncBlock sync("Sync", 0x55, 0x7E); // Preamble + sync word
    PacketDecodeBlock decoder("Decoder");
    
    // Visualization
    PlotCSpectrumBlock spectrum("Spectrum", 1024, sample_rate);
    PlotTimeSeriesBlock bits_plot("Bits", {"Demodulated Bits"}, 100, 5.0f);
    
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &resampler.in, &spectrum.in[0]),
        cler::BlockRunner(&resampler, &demod.in),
        cler::BlockRunner(&demod, &sync.in, &bits_plot.in[0]),
        cler::BlockRunner(&sync, &decoder.in),
        cler::BlockRunner(&decoder), // Outputs to callback
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&bits_plot)
    );
    
    // Set up packet callback
    decoder.set_packet_callback([](const std::vector<uint8_t>& packet) {
        std::cout << "Received packet: ";
        for (auto byte : packet) {
            printf("%02X ", byte);
        }
        std::cout << std::endl;
    });
    
    flowgraph.run();
    
    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        bits_plot.render();
        gui.end_frame();
    }
    
    return 0;
}`,
    features: [
      'Advanced GMSK demodulation',
      'Multistage resampling',
      'Packet synchronization',
      'Real-time bit visualization',
      'Callback-based packet output'
    ],
    output: 'Decoded wireless packets with visualization'
  }
];

export default function ExamplesSection() {
  const [selectedExample, setSelectedExample] = useState(0);

  const getComplexityColor = (complexity: string) => {
    switch (complexity) {
      case 'Beginner': return 'bg-green-100 text-green-800';
      case 'Intermediate': return 'bg-yellow-100 text-yellow-800';
      case 'Advanced': return 'bg-red-100 text-red-800';
      default: return 'bg-gray-100 text-gray-800';
    }
  };

  return (
    <div className="space-y-8">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-gray-900 mb-4">
          📡 Real Examples
        </h1>
        <p className="text-xl text-gray-600 mb-8">
          See CLER in action with real-world applications that you can run today
        </p>
      </div>

      {/* Example Selection */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        {examples.map((example, index) => (
          <button
            key={example.id}
            onClick={() => setSelectedExample(index)}
            className={`text-left p-6 rounded-xl border-2 transition-all ${
              selectedExample === index
                ? 'border-indigo-500 bg-indigo-50'
                : 'border-gray-200 hover:border-gray-300'
            }`}
          >
            <div className="flex items-center justify-between mb-3">
              <example.icon className={`w-8 h-8 ${
                selectedExample === index ? 'text-indigo-600' : 'text-gray-600'
              }`} />
              <span className={`px-2 py-1 rounded-full text-xs font-medium ${
                getComplexityColor(example.complexity)
              }`}>
                {example.complexity}
              </span>
            </div>
            
            <h3 className="text-lg font-bold text-gray-900 mb-2">
              {example.title}
            </h3>
            <p className="text-gray-600 text-sm mb-3">
              {example.description}
            </p>
            
            <div className="flex flex-wrap gap-1">
              {example.tags.map((tag, i) => (
                <span 
                  key={i}
                  className="px-2 py-1 bg-gray-100 text-gray-700 rounded text-xs"
                >
                  {tag}
                </span>
              ))}
            </div>
          </button>
        ))}
      </div>

      {/* Selected Example Details */}
      <div className="bg-white rounded-xl border-2 border-gray-200 p-8">
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
          {/* Code */}
          <div className="space-y-4">
            <div className="flex items-center justify-between">
              <h3 className="text-xl font-bold text-gray-900">
                {examples[selectedExample].title}
              </h3>
              <div className="flex items-center space-x-2">
                <span className={`px-2 py-1 rounded-full text-xs font-medium ${
                  getComplexityColor(examples[selectedExample].complexity)
                }`}>
                  {examples[selectedExample].complexity}
                </span>
                <button className="flex items-center space-x-1 text-sm text-indigo-600 hover:text-indigo-700">
                  <ExternalLink className="w-4 h-4" />
                  <span>View Full Code</span>
                </button>
              </div>
            </div>
            
            <CodeEditor
              code={examples[selectedExample].code}
              language="cpp"
              readOnly={true}
              height="500px"
            />
          </div>

          {/* Features and Output */}
          <div className="space-y-6">
            <div>
              <h4 className="text-lg font-semibold text-gray-900 mb-3">
                🔧 Key Features:
              </h4>
              <ul className="space-y-2">
                {examples[selectedExample].features.map((feature, index) => (
                  <li key={index} className="flex items-start space-x-2">
                    <div className="w-2 h-2 bg-indigo-600 rounded-full mt-2 flex-shrink-0" />
                    <span className="text-gray-700">{feature}</span>
                  </li>
                ))}
              </ul>
            </div>

            <div>
              <h4 className="text-lg font-semibold text-gray-900 mb-3">
                📊 Expected Output:
              </h4>
              <div className="bg-gray-50 rounded-lg p-4">
                <p className="text-gray-700">{examples[selectedExample].output}</p>
              </div>
            </div>

            {/* Try It Out */}
            <div className="bg-gradient-to-r from-indigo-50 to-purple-50 rounded-lg p-6">
              <h4 className="text-lg font-semibold text-gray-900 mb-3">
                🚀 Try It Out:
              </h4>
              <div className="space-y-3">
                <div className="bg-gray-900 rounded-lg p-3 text-green-400 font-mono text-sm">
                  <div>$ cd desktop_examples</div>
                  <div>$ ./{examples[selectedExample].id === 'physics' ? 'mass_spring_damper' : 
                          examples[selectedExample].id === 'sdr' ? 'hackrf_receiver' : 
                          'ezgmsk_demod/main'}</div>
                </div>
                
                <div className="flex items-center space-x-2">
                  <Play className="w-4 h-4 text-indigo-600" />
                  <span className="text-sm text-gray-600">
                    Example runs in real-time with interactive visualization
                  </span>
                </div>
              </div>
            </div>

            {/* Performance Note */}
            <div className="bg-blue-50 rounded-lg p-4">
              <h4 className="font-semibold text-blue-900 mb-2">⚡ Performance Note:</h4>
              <p className="text-sm text-blue-800">
                {selectedExample === 0 && "Runs at 1000 Hz with sub-millisecond latency"}
                {selectedExample === 1 && "Processes 2 MHz bandwidth in real-time"}
                {selectedExample === 2 && "Demodulates 38.4 kbps with hardware-accelerated DSP"}
              </p>
            </div>
          </div>
        </div>
      </div>

      {/* Build and Run Instructions */}
      <div className="bg-gray-50 rounded-lg p-6">
        <h3 className="text-lg font-semibold text-gray-900 mb-4">
          🏗️ Build Instructions:
        </h3>
        <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
          <div>
            <h4 className="font-semibold text-gray-900 mb-2">Prerequisites:</h4>
            <ul className="text-sm text-gray-700 space-y-1">
              <li>• CMake 3.16+</li>
              <li>• C++17 compiler</li>
              <li>• liquid-dsp (for DSP functions)</li>
              <li>• ImGui + OpenGL (for visualization)</li>
              <li>• HackRF library (for SDR example)</li>
            </ul>
          </div>
          <div>
            <h4 className="font-semibold text-gray-900 mb-2">Build Steps:</h4>
            <div className="bg-gray-900 rounded-lg p-3 text-green-400 font-mono text-sm">
              <div>$ mkdir build && cd build</div>
              <div>$ cmake ..</div>
              <div>$ make -j$(nproc)</div>
              <div>$ cd desktop_examples</div>
              <div>$ ./hello_world</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}