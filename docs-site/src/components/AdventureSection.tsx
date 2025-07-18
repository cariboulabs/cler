'use client';

import { useState } from 'react';
import { GitBranch, Settings, ArrowRight, CheckCircle, Code } from 'lucide-react';
import CodeEditor from './CodeEditor';

const flowgraphExample = `// Flowgraph Approach: Automatic threading
int main() {
    // Create blocks
    SourceCWBlock<float> source("Source", 1.0f, 10.0f, 1000);
    GainBlock<float> gain("Gain", 2.0f);
    SinkFileBlock<float> sink("Sink", "output.bin");
    
    // Define the flowgraph
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &gain.in),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );
    
    // Start processing (creates threads automatically)
    flowgraph.run();
    
    // Let it run for a while
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Stop and get statistics
    flowgraph.stop();
    auto stats = flowgraph.stats();
    
    return 0;
}`;

const streamlinedExample = `// Streamlined Approach: Manual control
int main() {
    // Create blocks
    SourceCWBlock<float> source("Source", 1.0f, 10.0f, 1000);
    GainBlock<float> gain("Gain", 2.0f);
    SinkFileBlock<float> sink("Sink", "output.bin");
    
    // Main processing loop (you control everything)
    while (running) {
        // Run each block's procedure manually
        auto source_result = source.procedure(&gain.in);
        if (source_result.is_err()) {
            // Handle source errors
        }
        
        auto gain_result = gain.procedure(&sink.in);
        if (gain_result.is_err()) {
            // Handle gain errors
        }
        
        auto sink_result = sink.procedure(nullptr);
        if (sink_result.is_err()) {
            // Handle sink errors
        }
        
        // You decide when to yield, sleep, etc.
        std::this_thread::yield();
    }
    
    return 0;
}`;

const approaches = [
  {
    id: 'flowgraph',
    title: 'FlowGraph',
    subtitle: 'Automatic Threading',
    icon: GitBranch,
    description: 'Let CLER handle the threading, synchronization, and error recovery automatically.',
    pros: [
      'Automatic thread management',
      'Built-in error handling and recovery',
      'Adaptive power management',
      'Comprehensive statistics',
      'Easier to debug and profile'
    ],
    cons: [
      'Thread overhead for simple operations',
      'Less control over execution timing',
      'May not be suitable for hard real-time'
    ],
    bestFor: [
      'Complex signal processing pipelines',
      'Desktop applications',
      'Prototyping and experimentation',
      'Applications with varying computational load'
    ],
    code: flowgraphExample
  },
  {
    id: 'streamlined',
    title: 'Streamlined',
    subtitle: 'Manual Control',
    icon: Settings,
    description: 'Take full control of the execution flow, timing, and resource management.',
    pros: [
      'Zero threading overhead',
      'Precise timing control',
      'Deterministic execution',
      'Lower memory footprint',
      'Better for real-time systems'
    ],
    cons: [
      'More complex error handling',
      'Manual synchronization required',
      'More boilerplate code',
      'Harder to scale complex pipelines'
    ],
    bestFor: [
      'Embedded systems',
      'Real-time applications',
      'Simple processing chains',
      'Performance-critical applications'
    ],
    code: streamlinedExample
  }
];

export default function AdventureSection() {
  const [selectedApproach, setSelectedApproach] = useState(0);

  return (
    <div className="space-y-8">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-gray-900 mb-4">
          🎮 Choose Your Adventure
        </h1>
        <p className="text-xl text-gray-600 mb-8">
          CLER supports two architectural styles. Pick the one that matches your needs.
        </p>
      </div>

      {/* Approach Selection */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        {approaches.map((approach, index) => (
          <button
            key={approach.id}
            onClick={() => setSelectedApproach(index)}
            className={`text-left p-6 rounded-xl border-2 transition-all ${
              selectedApproach === index
                ? 'border-indigo-500 bg-indigo-50'
                : 'border-gray-200 hover:border-gray-300'
            }`}
          >
            <div className="flex items-center space-x-3 mb-4">
              <approach.icon className={`w-8 h-8 ${
                selectedApproach === index ? 'text-indigo-600' : 'text-gray-600'
              }`} />
              <div>
                <h3 className="text-xl font-bold text-gray-900">{approach.title}</h3>
                <p className="text-gray-600">{approach.subtitle}</p>
              </div>
            </div>
            <p className="text-gray-700 mb-4">{approach.description}</p>
            
            <div className="space-y-3">
              <div>
                <h4 className="font-semibold text-green-700 mb-2">✅ Pros:</h4>
                <ul className="space-y-1 text-sm">
                  {approach.pros.map((pro, i) => (
                    <li key={i} className="flex items-start space-x-2">
                      <CheckCircle className="w-4 h-4 text-green-500 mt-0.5 flex-shrink-0" />
                      <span>{pro}</span>
                    </li>
                  ))}
                </ul>
              </div>
              
              <div>
                <h4 className="font-semibold text-orange-700 mb-2">⚠️ Cons:</h4>
                <ul className="space-y-1 text-sm">
                  {approach.cons.map((con, i) => (
                    <li key={i} className="flex items-start space-x-2">
                      <div className="w-4 h-4 bg-orange-500 rounded-full mt-0.5 flex-shrink-0" />
                      <span>{con}</span>
                    </li>
                  ))}
                </ul>
              </div>
            </div>
          </button>
        ))}
      </div>

      {/* Selected Approach Details */}
      <div className="bg-white rounded-xl border-2 border-gray-200 p-8">
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
          {/* Code Example */}
          <div className="space-y-4">
            <h3 className="text-xl font-bold text-gray-900 flex items-center space-x-2">
              <Code className="w-6 h-6 text-indigo-600" />
              <span>{approaches[selectedApproach].title} Implementation</span>
            </h3>
            <CodeEditor
              code={approaches[selectedApproach].code}
              language="cpp"
              readOnly={true}
              height="400px"
            />
          </div>

          {/* Best For */}
          <div className="space-y-6">
            <div>
              <h4 className="text-lg font-semibold text-gray-900 mb-3">
                🎯 Best For:
              </h4>
              <ul className="space-y-2">
                {approaches[selectedApproach].bestFor.map((use, index) => (
                  <li key={index} className="flex items-start space-x-2">
                    <ArrowRight className="w-4 h-4 text-indigo-600 mt-0.5 flex-shrink-0" />
                    <span className="text-gray-700">{use}</span>
                  </li>
                ))}
              </ul>
            </div>

            {/* Performance Comparison */}
            <div className="bg-gray-50 rounded-lg p-4">
              <h4 className="font-semibold text-gray-900 mb-3">⚡ Performance Profile:</h4>
              <div className="space-y-2">
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600">Simplicity</span>
                  <div className="w-32 bg-gray-200 rounded-full h-2">
                    <div 
                      className="bg-indigo-600 h-2 rounded-full"
                      style={{ width: selectedApproach === 0 ? '85%' : '45%' }}
                    />
                  </div>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600">Performance</span>
                  <div className="w-32 bg-gray-200 rounded-full h-2">
                    <div 
                      className="bg-green-600 h-2 rounded-full"
                      style={{ width: selectedApproach === 0 ? '70%' : '90%' }}
                    />
                  </div>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-gray-600">Control</span>
                  <div className="w-32 bg-gray-200 rounded-full h-2">
                    <div 
                      className="bg-purple-600 h-2 rounded-full"
                      style={{ width: selectedApproach === 0 ? '40%' : '95%' }}
                    />
                  </div>
                </div>
              </div>
            </div>

            {/* Hybrid Approach */}
            <div className="bg-blue-50 rounded-lg p-4">
              <h4 className="font-semibold text-blue-900 mb-2">💡 Pro Tip:</h4>
              <p className="text-sm text-blue-800">
                You can combine both approaches! Use FlowGraph for complex pipelines and 
                Streamlined for performance-critical inner loops. Create "SuperBlocks" 
                that internally use streamlined processing but interface with the FlowGraph system.
              </p>
            </div>
          </div>
        </div>
      </div>

      {/* Decision Helper */}
      <div className="bg-gradient-to-r from-green-50 to-blue-50 rounded-xl p-6">
        <h3 className="text-lg font-bold text-gray-900 mb-4 text-center">
          🤔 Still Not Sure? Quick Decision Tree:
        </h3>
        <div className="space-y-3 text-sm">
          <div className="flex items-center space-x-2">
            <div className="w-2 h-2 bg-green-500 rounded-full" />
            <span><strong>Building a prototype?</strong> → Start with FlowGraph</span>
          </div>
          <div className="flex items-center space-x-2">
            <div className="w-2 h-2 bg-blue-500 rounded-full" />
            <span><strong>Targeting embedded systems?</strong> → Consider Streamlined</span>
          </div>
          <div className="flex items-center space-x-2">
            <div className="w-2 h-2 bg-purple-500 rounded-full" />
            <span><strong>Need precise timing?</strong> → Go with Streamlined</span>
          </div>
          <div className="flex items-center space-x-2">
            <div className="w-2 h-2 bg-orange-500 rounded-full" />
            <span><strong>Complex multi-stage processing?</strong> → FlowGraph is your friend</span>
          </div>
        </div>
      </div>
    </div>
  );
}