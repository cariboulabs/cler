'use client';

import { useState } from 'react';
import { Play, Terminal, CheckCircle, Code } from 'lucide-react';
import CodeEditor from './CodeEditor';

interface HelloSectionProps {
  onStart: () => void;
}

const helloWorldCode = `#include <cler.hpp>
#include <cler_desktop_utils.hpp>

int main() {
    cler::GuiManager gui(800, 400, "Hello World Plot Example");

    const size_t SPS = 1000;
    SourceCWBlock<float> source1("CWSource", 1.0f, 1.0f, SPS); //amplitude, frequency
    SourceCWBlock<float> source2("CWSource2", 1.0f, 20.0f, SPS);
    ThrottleBlock<float> throttle("Throttle", SPS);
    AddBlock<float> adder("Adder", 2); // 2 inputs

    PlotTimeSeriesBlock plot(
        "Hello World Plot",
        {"Added Sources"},
        SPS,
        3.0f // duration in seconds
    );
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source1, &adder.in[0]),
        cler::BlockRunner(&source2, &adder.in[1]),
        cler::BlockRunner(&adder, &throttle.in),
        cler::BlockRunner(&throttle, &plot.in[0]),
        cler::BlockRunner(&plot)
    );

    flowgraph.run();

    while (gui.should_close() == false) {
        gui.begin_frame();
        plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}`;

const buildSteps = [
  { command: 'mkdir build && cd build', description: 'Create build directory' },
  { command: 'cmake ..', description: 'Generate build files' },
  { command: 'make -j"$(nproc --ignore=1)"', description: 'Compile (using all cores-1)' },
  { command: 'cd desktop_examples', description: 'Navigate to examples' },
  { command: './hello_world', description: 'Run the hello world example' },
];

export default function HelloSection({ onStart }: HelloSectionProps) {
  const [currentStep, setCurrentStep] = useState(0);
  const [isRunning, setIsRunning] = useState(false);

  const startDemo = () => {
    setIsRunning(true);
    onStart();
    
    // Simulate building process
    const timer = setInterval(() => {
      setCurrentStep((prev) => {
        if (prev >= buildSteps.length - 1) {
          clearInterval(timer);
          setIsRunning(false);
          return prev;
        }
        return prev + 1;
      });
    }, 1000);
  };

  return (
    <div className="space-y-8">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-gray-900 mb-4">
          🚀 Hello in 60 Seconds
        </h1>
        <p className="text-xl text-gray-600 mb-8">
          Let's get you up and running with CLER in under a minute. 
          No long explanations - just working code.
        </p>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
        {/* Code Editor */}
        <div className="space-y-4">
          <div className="flex items-center space-x-2">
            <Code className="w-5 h-5 text-indigo-600" />
            <h3 className="text-lg font-semibold">hello_world.cpp</h3>
          </div>
          <CodeEditor 
            code={helloWorldCode}
            language="cpp"
            readOnly={false}
            onCodeChange={(code) => {
              // Allow users to modify the code
              console.log('Code changed:', code);
            }}
          />
        </div>

        {/* Terminal / Build Process */}
        <div className="space-y-4">
          <div className="flex items-center space-x-2">
            <Terminal className="w-5 h-5 text-indigo-600" />
            <h3 className="text-lg font-semibold">Build & Run</h3>
          </div>
          
          <div className="bg-gray-900 rounded-lg p-4 text-green-400 font-mono text-sm">
            <div className="space-y-2">
              {buildSteps.map((step, index) => (
                <div 
                  key={index}
                  className={`flex items-center space-x-2 ${
                    index < currentStep ? 'text-green-400' : 
                    index === currentStep ? 'text-yellow-400' : 
                    'text-gray-600'
                  }`}
                >
                  {index < currentStep ? (
                    <CheckCircle className="w-4 h-4" />
                  ) : index === currentStep ? (
                    <div className="w-4 h-4 border-2 border-yellow-400 rounded-full animate-spin border-t-transparent" />
                  ) : (
                    <div className="w-4 h-4 border-2 border-gray-600 rounded-full" />
                  )}
                  <span className="text-white">$ {step.command}</span>
                </div>
              ))}
            </div>
          </div>

          <div className="text-center">
            <button
              onClick={startDemo}
              disabled={isRunning}
              className="inline-flex items-center space-x-2 px-6 py-3 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
            >
              <Play className="w-5 h-5" />
              <span>{isRunning ? 'Building...' : 'Start Demo'}</span>
            </button>
          </div>
        </div>
      </div>

      {/* Visual Output Simulation */}
      {currentStep >= buildSteps.length && (
        <div className="bg-gray-100 rounded-lg p-6 text-center">
          <h4 className="text-lg font-semibold mb-4">🎉 Success! Your first CLER program is running</h4>
          <div className="bg-white rounded-lg p-4 border-2 border-gray-200">
            <div className="text-sm text-gray-600 mb-2">Simulated Plot Output:</div>
            <div className="h-32 bg-gradient-to-r from-blue-400 to-purple-500 rounded flex items-center justify-center">
              <div className="text-white font-semibold">
                📊 Real-time waveform visualization
              </div>
            </div>
          </div>
          <p className="text-sm text-gray-600 mt-4">
            This creates two sine waves (1Hz and 20Hz), adds them together, and displays the result in real-time.
          </p>
        </div>
      )}

      {/* Key Takeaways */}
      <div className="bg-indigo-50 rounded-lg p-6">
        <h4 className="text-lg font-semibold text-indigo-900 mb-3">✨ What just happened?</h4>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 text-sm">
          <div>
            <div className="font-semibold text-indigo-800">Created Blocks</div>
            <div className="text-indigo-600">2 signal sources, 1 adder, 1 throttle, 1 plot</div>
          </div>
          <div>
            <div className="font-semibold text-indigo-800">Connected Pipes</div>
            <div className="text-indigo-600">Typed channels carry data between blocks</div>
          </div>
          <div>
            <div className="font-semibold text-indigo-800">Ran FlowGraph</div>
            <div className="text-indigo-600">Automatic threading and data flow</div>
          </div>
        </div>
      </div>
    </div>
  );
}