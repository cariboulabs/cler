'use client';

import { useState } from 'react';
import { 
  Wrench, 
  BookOpen, 
  Users, 
  GitFork, 
  ExternalLink, 
  ArrowRight,
  CheckCircle,
  Lightbulb,
  MessageSquare
} from 'lucide-react';

const nextSteps = [
  {
    id: 'blocks',
    title: 'Browse Block Library',
    description: 'Explore the complete collection of pre-built blocks',
    icon: BookOpen,
    action: 'Browse Library',
    url: '/blocks',
    items: [
      'Signal Sources (CW, Chirp, File, Hardware)',
      'Math Operations (Add, Multiply, Gain, FFT)',
      'Filters (FIR, IIR, Polyphase)',
      'Visualizations (Plots, Spectrograms)',
      'Sinks (File, UDP, Audio, Null)'
    ]
  },
  {
    id: 'embedded',
    title: 'Embedded Development',
    description: 'Deploy CLER on microcontrollers and embedded systems',
    icon: Wrench,
    action: 'See Embedded Guide',
    url: '/embedded',
    items: [
      'FreeRTOS task policy integration',
      'Static memory allocation patterns',
      'Bare-metal examples',
      'Memory-constrained optimizations',
      'Real-time scheduling guidelines'
    ]
  },
  {
    id: 'community',
    title: 'Join the Community',
    description: 'Connect with other CLER developers and contributors',
    icon: Users,
    action: 'Join Community',
    url: 'https://github.com/your-org/cler/discussions',
    items: [
      'GitHub Discussions for Q&A',
      'Share your CLER projects',
      'Request new features',
      'Get help with complex implementations',
      'Contribute to the ecosystem'
    ]
  }
];

const quickReference = [
  {
    category: 'Core Concepts',
    items: [
      { name: 'Block', desc: 'Processing unit with procedure() method' },
      { name: 'Channel', desc: 'Typed SPSC queue connecting blocks' },
      { name: 'FlowGraph', desc: 'Manages block execution and threading' },
      { name: 'TaskPolicy', desc: 'Abstraction for different thread models' }
    ]
  },
  {
    category: 'Common Patterns',
    items: [
      { name: 'Source → Process → Sink', desc: 'Basic linear processing chain' },
      { name: 'Fanout', desc: 'One block feeding multiple outputs' },
      { name: 'Superblock', desc: 'Combining multiple blocks for efficiency' },
      { name: 'Callback', desc: 'Async communication between blocks' }
    ]
  },
  {
    category: 'Best Practices',
    items: [
      { name: 'Template everything', desc: 'Maximize compile-time optimization' },
      { name: 'Use Read/Write', desc: 'Prefer bulk operations over push/pop' },
      { name: 'Static allocation', desc: 'Avoid heap allocation in embedded' },
      { name: 'Error handling', desc: 'Always check Result<T, Error> returns' }
    ]
  }
];

export default function BuildSection() {
  const [completedSteps, setCompletedSteps] = useState<string[]>([]);

  const toggleStep = (stepId: string) => {
    setCompletedSteps(prev => 
      prev.includes(stepId) 
        ? prev.filter(id => id !== stepId)
        : [...prev, stepId]
    );
  };

  return (
    <div className="space-y-8">
      <div className="text-center">
        <h1 className="text-4xl font-bold text-gray-900 mb-4">
          🔧 Go Build
        </h1>
        <p className="text-xl text-gray-600 mb-8">
          You've learned the basics. Now let's turn you into a CLER expert!
        </p>
      </div>

      {/* Congratulations */}
      <div className="bg-gradient-to-r from-green-50 to-blue-50 rounded-xl p-8 text-center">
        <div className="text-6xl mb-4">🎉</div>
        <h2 className="text-2xl font-bold text-gray-900 mb-4">
          Congratulations! You've completed the 30-minute CLER onboarding.
        </h2>
        <p className="text-gray-600 mb-6">
          You now understand blocks, channels, flowgraphs, and the two architectural approaches. 
          Time to build something amazing!
        </p>
        <div className="flex justify-center items-center space-x-4 text-sm text-gray-600">
          <CheckCircle className="w-5 h-5 text-green-500" />
          <span>Core concepts mastered</span>
          <CheckCircle className="w-5 h-5 text-green-500" />
          <span>Examples explored</span>
          <CheckCircle className="w-5 h-5 text-green-500" />
          <span>Architecture chosen</span>
        </div>
      </div>

      {/* Next Steps */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        {nextSteps.map((step) => (
          <div
            key={step.id}
            className={`border-2 rounded-xl p-6 transition-all ${
              completedSteps.includes(step.id)
                ? 'border-green-500 bg-green-50'
                : 'border-gray-200 hover:border-gray-300'
            }`}
          >
            <div className="flex items-center justify-between mb-4">
              <step.icon className={`w-8 h-8 ${
                completedSteps.includes(step.id) ? 'text-green-600' : 'text-gray-600'
              }`} />
              <button
                onClick={() => toggleStep(step.id)}
                className={`w-6 h-6 rounded-full border-2 flex items-center justify-center ${
                  completedSteps.includes(step.id)
                    ? 'border-green-500 bg-green-500'
                    : 'border-gray-300 hover:border-gray-400'
                }`}
              >
                {completedSteps.includes(step.id) && (
                  <CheckCircle className="w-4 h-4 text-white" />
                )}
              </button>
            </div>
            
            <h3 className="text-xl font-bold text-gray-900 mb-2">
              {step.title}
            </h3>
            <p className="text-gray-600 mb-4">
              {step.description}
            </p>
            
            <ul className="space-y-2 mb-6">
              {step.items.map((item, index) => (
                <li key={index} className="flex items-start space-x-2">
                  <ArrowRight className="w-4 h-4 text-indigo-600 mt-0.5 flex-shrink-0" />
                  <span className="text-sm text-gray-700">{item}</span>
                </li>
              ))}
            </ul>
            
            <button className="w-full flex items-center justify-center space-x-2 px-4 py-2 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700 transition-colors">
              <span>{step.action}</span>
              <ExternalLink className="w-4 h-4" />
            </button>
          </div>
        ))}
      </div>

      {/* Quick Reference */}
      <div className="bg-white rounded-xl border-2 border-gray-200 p-8">
        <h3 className="text-2xl font-bold text-gray-900 mb-6 text-center">
          📚 Quick Reference Guide
        </h3>
        
        <div className="grid grid-cols-1 md:grid-cols-3 gap-8">
          {quickReference.map((section) => (
            <div key={section.category}>
              <h4 className="text-lg font-semibold text-gray-900 mb-4">
                {section.category}
              </h4>
              <div className="space-y-3">
                {section.items.map((item, index) => (
                  <div key={index} className="bg-gray-50 rounded-lg p-3">
                    <div className="font-semibold text-gray-900 text-sm">
                      {item.name}
                    </div>
                    <div className="text-xs text-gray-600 mt-1">
                      {item.desc}
                    </div>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Create Your First Project */}
      <div className="bg-indigo-50 rounded-xl p-8">
        <div className="text-center mb-6">
          <Lightbulb className="w-12 h-12 text-indigo-600 mx-auto mb-4" />
          <h3 className="text-xl font-bold text-gray-900 mb-2">
            Ready to Create Your First Project?
          </h3>
          <p className="text-gray-600">
            Here are some project ideas to get you started:
          </p>
        </div>
        
        <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
          <div className="bg-white rounded-lg p-4">
            <h4 className="font-semibold text-gray-900 mb-2">🔊 Audio Projects</h4>
            <ul className="text-sm text-gray-700 space-y-1">
              <li>• Real-time audio effects processor</li>
              <li>• Digital filter design tool</li>
              <li>• Audio spectrum analyzer</li>
              <li>• Voice activity detector</li>
            </ul>
          </div>
          
          <div className="bg-white rounded-lg p-4">
            <h4 className="font-semibold text-gray-900 mb-2">📡 Radio Projects</h4>
            <ul className="text-sm text-gray-700 space-y-1">
              <li>• ADS-B aircraft tracker</li>
              <li>• Weather satellite decoder</li>
              <li>• Ham radio digital modes</li>
              <li>• ISM band protocol analyzer</li>
            </ul>
          </div>
          
          <div className="bg-white rounded-lg p-4">
            <h4 className="font-semibold text-gray-900 mb-2">🔬 Embedded Projects</h4>
            <ul className="text-sm text-gray-700 space-y-1">
              <li>• IoT sensor fusion</li>
              <li>• Real-time control systems</li>
              <li>• Data acquisition system</li>
              <li>• Edge AI preprocessing</li>
            </ul>
          </div>
          
          <div className="bg-white rounded-lg p-4">
            <h4 className="font-semibold text-gray-900 mb-2">🎮 Fun Projects</h4>
            <ul className="text-sm text-gray-700 space-y-1">
              <li>• Music synthesizer</li>
              <li>• Game physics engine</li>
              <li>• Procedural signal generation</li>
              <li>• Interactive data visualization</li>
            </ul>
          </div>
        </div>
      </div>

      {/* Community and Support */}
      <div className="bg-gray-50 rounded-xl p-8">
        <div className="text-center mb-6">
          <MessageSquare className="w-12 h-12 text-indigo-600 mx-auto mb-4" />
          <h3 className="text-xl font-bold text-gray-900 mb-2">
            Need Help or Want to Share?
          </h3>
          <p className="text-gray-600">
            The CLER community is here to help you succeed
          </p>
        </div>
        
        <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
          <div className="text-center">
            <GitFork className="w-8 h-8 text-gray-600 mx-auto mb-2" />
            <h4 className="font-semibold text-gray-900 mb-2">Contribute</h4>
            <p className="text-sm text-gray-600 mb-3">
              Found a bug? Want to add a feature? Contributions are welcome!
            </p>
            <button className="px-4 py-2 bg-gray-800 text-white rounded-lg hover:bg-gray-900 transition-colors">
              View on GitHub
            </button>
          </div>
          
          <div className="text-center">
            <Users className="w-8 h-8 text-gray-600 mx-auto mb-2" />
            <h4 className="font-semibold text-gray-900 mb-2">Discuss</h4>
            <p className="text-sm text-gray-600 mb-3">
              Ask questions, share projects, and connect with other developers
            </p>
            <button className="px-4 py-2 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700 transition-colors">
              Join Discussions
            </button>
          </div>
        </div>
      </div>

      {/* Final Call to Action */}
      <div className="text-center bg-gradient-to-r from-indigo-600 to-purple-600 text-white rounded-xl p-8">
        <h3 className="text-2xl font-bold mb-4">
          🚀 Start Building with CLER Today!
        </h3>
        <p className="text-lg mb-6 opacity-90">
          You have everything you need to create amazing DSP applications
        </p>
        <div className="flex justify-center space-x-4">
          <button className="px-6 py-3 bg-white text-indigo-600 rounded-lg font-semibold hover:bg-gray-100 transition-colors">
            Download CLER
          </button>
          <button className="px-6 py-3 bg-indigo-800 text-white rounded-lg font-semibold hover:bg-indigo-900 transition-colors">
            View Examples
          </button>
        </div>
      </div>
    </div>
  );
}