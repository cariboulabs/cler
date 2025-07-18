'use client';

import React, { useState } from 'react';
import { Box, ArrowRight, Zap, Code, Cpu } from 'lucide-react';

const concepts = [
  {
    id: 'block',
    title: 'Block',
    subtitle: 'A function with state',
    icon: Box,
    description: 'Blocks are the building units of CLER. Each block has a procedure() method that processes data.',
    example: `struct GainBlock : public BlockBase {
    float gain;
    Channel<float> in;
    
    Result<Empty, Error> procedure(ChannelBase<float>* out) {
        float sample;
        if (in.size() > 0) {
            in.pop(sample);
            out->push(sample * gain);
            return Ok(Empty{});
        }
        return Err(Error::NotEnoughSamples);
    }
};`,
    details: [
      'Implements a procedure() method for data processing',
      'Owns its input channels as member variables',
      'Returns Result<Empty, Error> for error handling',
      'Can have multiple inputs and outputs'
    ]
  },
  {
    id: 'channel',
    title: 'Channel',
    subtitle: 'Typed pipe between blocks',
    icon: ArrowRight,
    description: 'Channels are lock-free, single-producer single-consumer queues that connect blocks.',
    example: `// Stack allocated channel
Channel<float, 1024> static_channel;

// Heap allocated channel  
Channel<float> dynamic_channel(1024);

// Three access patterns:
// 1. Push/Pop (slow - avoid)
channel.push(value);
channel.pop(value);

// 2. Peek/Commit (manual)
auto [ptr, size] = channel.peek_read();
// process data...
channel.commit_read(processed_count);

// 3. Read/Write (recommended)
auto count = channel.readN(buffer, max_items);`,
    details: [
      'Template-based with compile-time or runtime sizing',
      'Lock-free SPSC (Single Producer Single Consumer)',
      'Three access patterns: Push/Pop, Peek/Commit, Read/Write',
      'Type-safe - compile-time type checking'
    ]
  },
  {
    id: 'flowgraph',
    title: 'FlowGraph',
    subtitle: 'Orchestrates everything',
    icon: Zap,
    description: 'FlowGraph manages the execution of blocks using a task policy (threads, FreeRTOS, etc.).',
    example: `auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&source, &gain.in),
    cler::BlockRunner(&gain, &sink.in),
    cler::BlockRunner(&sink)
);

flowgraph.run();

// Later...
flowgraph.stop();
auto stats = flowgraph.stats();`,
    details: [
      'Creates one thread per block (on desktop)',
      'Automatic error handling and recovery',
      'Adaptive sleep for power efficiency',
      'Comprehensive execution statistics'
    ]
  }
];

export default function ClerWaySection() {
  const [selectedConcept, setSelectedConcept] = useState(0);

  return (
    <div className="space-y-8">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-gray-900 mb-4">
          🧠 The CLER Way
        </h1>
        <p className="text-xl text-gray-600 mb-8">
          Understanding CLER's core philosophy: simple concepts, powerful combinations
        </p>
      </div>

      {/* Concept Navigator */}
      <div className="flex justify-center space-x-4">
        {concepts.map((concept, index) => (
          <button
            key={concept.id}
            onClick={() => setSelectedConcept(index)}
            className={`flex items-center space-x-2 px-4 py-3 rounded-lg transition-colors ${
              selectedConcept === index
                ? 'bg-indigo-600 text-white'
                : 'bg-gray-100 text-gray-700 hover:bg-gray-200'
            }`}
          >
            <concept.icon className="w-5 h-5" />
            <div className="text-left">
              <div className="font-semibold">{concept.title}</div>
              <div className="text-sm opacity-75">{concept.subtitle}</div>
            </div>
          </button>
        ))}
      </div>

      {/* Selected Concept Details */}
      <div className="bg-white rounded-xl border-2 border-gray-200 p-8">
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
          {/* Description */}
          <div className="space-y-6">
            <div className="flex items-center space-x-3">
              {React.createElement(concepts[selectedConcept].icon, { 
                className: "w-8 h-8 text-indigo-600" 
              })}
              <div>
                <h3 className="text-2xl font-bold text-gray-900">
                  {concepts[selectedConcept].title}
                </h3>
                <p className="text-gray-600">{concepts[selectedConcept].subtitle}</p>
              </div>
            </div>

            <p className="text-gray-700 text-lg leading-relaxed">
              {concepts[selectedConcept].description}
            </p>

            <div className="space-y-3">
              <h4 className="font-semibold text-gray-900">Key Features:</h4>
              <ul className="space-y-2">
                {concepts[selectedConcept].details.map((detail, index) => (
                  <li key={index} className="flex items-start space-x-2">
                    <div className="w-2 h-2 bg-indigo-600 rounded-full mt-2 flex-shrink-0" />
                    <span className="text-gray-700">{detail}</span>
                  </li>
                ))}
              </ul>
            </div>
          </div>

          {/* Code Example */}
          <div className="space-y-4">
            <h4 className="font-semibold text-gray-900">Example Code:</h4>
            <div className="bg-gray-900 rounded-lg p-4 text-sm">
              <pre className="text-green-400 font-mono overflow-x-auto">
                <code>{concepts[selectedConcept].example}</code>
              </pre>
            </div>
          </div>
        </div>
      </div>

      {/* Interactive Flowgraph Diagram */}
      <div className="bg-gradient-to-r from-indigo-50 to-purple-50 rounded-xl p-8">
        <h3 className="text-xl font-bold text-gray-900 mb-6 text-center">
          Interactive FlowGraph Visualization
        </h3>
        
        <div className="flex items-center justify-center space-x-8">
          {/* Source Block */}
          <div className="bg-white rounded-lg p-4 border-2 border-indigo-200 shadow-md">
            <div className="flex items-center space-x-2 mb-2">
              <div className="w-3 h-3 bg-green-500 rounded-full" />
              <span className="font-semibold text-sm">Source</span>
            </div>
            <div className="text-xs text-gray-600">Generates data</div>
          </div>

          {/* Channel Arrow */}
          <div className="flex items-center space-x-1 text-indigo-600">
            <div className="w-12 h-1 bg-indigo-600 rounded" />
            <ArrowRight className="w-4 h-4" />
            <div className="text-xs">Channel&lt;float&gt;</div>
          </div>

          {/* Processing Block */}
          <div className="bg-white rounded-lg p-4 border-2 border-purple-200 shadow-md">
            <div className="flex items-center space-x-2 mb-2">
              <div className="w-3 h-3 bg-purple-500 rounded-full" />
              <span className="font-semibold text-sm">Gain</span>
            </div>
            <div className="text-xs text-gray-600">Amplifies signal</div>
          </div>

          {/* Channel Arrow */}
          <div className="flex items-center space-x-1 text-indigo-600">
            <div className="w-12 h-1 bg-indigo-600 rounded" />
            <ArrowRight className="w-4 h-4" />
            <div className="text-xs">Channel&lt;float&gt;</div>
          </div>

          {/* Sink Block */}
          <div className="bg-white rounded-lg p-4 border-2 border-red-200 shadow-md">
            <div className="flex items-center space-x-2 mb-2">
              <div className="w-3 h-3 bg-red-500 rounded-full" />
              <span className="font-semibold text-sm">Sink</span>
            </div>
            <div className="text-xs text-gray-600">Consumes data</div>
          </div>
        </div>

        <div className="text-center mt-6">
          <p className="text-gray-600 text-sm">
            Each block runs in its own thread, processing data as it becomes available
          </p>
        </div>
      </div>

      {/* Why This Matters */}
      <div className="bg-gray-50 rounded-lg p-6">
        <h4 className="text-lg font-semibold text-gray-900 mb-4">🎯 Why This Design?</h4>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
          <div className="text-center">
            <Cpu className="w-8 h-8 text-indigo-600 mx-auto mb-2" />
            <h5 className="font-semibold text-gray-900">Zero-Cost Abstraction</h5>
            <p className="text-sm text-gray-600">
              Templates and compile-time optimization mean no runtime overhead
            </p>
          </div>
          <div className="text-center">
            <Code className="w-8 h-8 text-indigo-600 mx-auto mb-2" />
            <h5 className="font-semibold text-gray-900">Type Safety</h5>
            <p className="text-sm text-gray-600">
              Catch errors at compile time, not runtime
            </p>
          </div>
          <div className="text-center">
            <Zap className="w-8 h-8 text-indigo-600 mx-auto mb-2" />
            <h5 className="font-semibold text-gray-900">Embedded Ready</h5>
            <p className="text-sm text-gray-600">
              Works on everything from MCUs to desktop systems
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}