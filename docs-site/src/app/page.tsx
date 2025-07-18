'use client';

import { useState, useEffect } from 'react';
import { ChevronRight, Clock, Zap, Code, Radio, Wrench } from 'lucide-react';
import HelloSection from '@/components/HelloSection';
import ClerWaySection from '@/components/ClerWaySection';
import AdventureSection from '@/components/AdventureSection';
import ExamplesSection from '@/components/ExamplesSection';
import BuildSection from '@/components/BuildSection';

const sections = [
  { id: 'hello', title: 'Hello in 60 Seconds', icon: Zap, time: 6 },
  { id: 'way', title: 'The CLER Way', icon: Code, time: 6 },
  { id: 'adventure', title: 'Choose Your Adventure', icon: Radio, time: 6 },
  { id: 'examples', title: 'Real Examples', icon: Radio, time: 6 },
  { id: 'build', title: 'Go Build', icon: Wrench, time: 6 },
];

export default function Home() {
  const [currentSection, setCurrentSection] = useState(0);
  const [timeRemaining, setTimeRemaining] = useState(30 * 60); // 30 minutes in seconds
  const [isTimerActive, setIsTimerActive] = useState(false);

  useEffect(() => {
    let interval: NodeJS.Timeout;
    if (isTimerActive && timeRemaining > 0) {
      interval = setInterval(() => {
        setTimeRemaining((time) => time - 1);
      }, 1000);
    }
    return () => clearInterval(interval);
  }, [isTimerActive, timeRemaining]);

  const startTimer = () => {
    setIsTimerActive(true);
  };

  const formatTime = (seconds: number) => {
    const mins = Math.floor(seconds / 60);
    const secs = seconds % 60;
    return `${mins}:${secs.toString().padStart(2, '0')}`;
  };

  const nextSection = () => {
    if (currentSection < sections.length - 1) {
      setCurrentSection(currentSection + 1);
    }
  };

  const prevSection = () => {
    if (currentSection > 0) {
      setCurrentSection(currentSection - 1);
    }
  };

  const renderCurrentSection = () => {
    switch (currentSection) {
      case 0:
        return <HelloSection onStart={startTimer} />;
      case 1:
        return <ClerWaySection />;
      case 2:
        return <AdventureSection />;
      case 3:
        return <ExamplesSection />;
      case 4:
        return <BuildSection />;
      default:
        return <HelloSection onStart={startTimer} />;
    }
  };

  return (
    <div className="min-h-screen bg-gradient-to-br from-blue-50 to-indigo-100">
      {/* Header */}
      <header className="bg-white shadow-sm">
        <div className="max-w-7xl mx-auto px-4 py-6">
          <div className="flex items-center justify-between">
            <div className="flex items-center space-x-4">
              <div className="text-2xl font-bold text-indigo-600">CLER</div>
              <div className="text-gray-600">
                Compile-Time DSP Flowgraph for SDRs and Embedded Systems
              </div>
            </div>
            <div className="flex items-center space-x-4">
              <div className="flex items-center space-x-2 bg-indigo-100 px-3 py-1 rounded-full">
                <Clock className="w-4 h-4 text-indigo-600" />
                <span className="text-indigo-600 font-mono">
                  {formatTime(timeRemaining)}
                </span>
              </div>
              <div className="text-sm text-gray-500">
                {currentSection + 1} of {sections.length}
              </div>
            </div>
          </div>
        </div>
      </header>

      {/* Progress Bar */}
      <div className="bg-white border-b">
        <div className="max-w-7xl mx-auto px-4">
          <div className="flex items-center justify-between py-2">
            {sections.map((section, index) => (
              <div
                key={section.id}
                className={`flex items-center space-x-2 px-3 py-2 rounded-lg cursor-pointer transition-colors ${
                  index === currentSection
                    ? 'bg-indigo-100 text-indigo-600'
                    : index < currentSection
                    ? 'bg-green-100 text-green-600'
                    : 'text-gray-400'
                }`}
                onClick={() => setCurrentSection(index)}
              >
                <section.icon className="w-4 h-4" />
                <span className="text-sm font-medium">{section.title}</span>
                <span className="text-xs">({section.time}m)</span>
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* Main Content */}
      <main className="max-w-7xl mx-auto px-4 py-8">
        <div className="bg-white rounded-xl shadow-lg p-8">
          {renderCurrentSection()}
        </div>
      </main>

      {/* Navigation */}
      <div className="max-w-7xl mx-auto px-4 py-6">
        <div className="flex justify-between items-center">
          <button
            onClick={prevSection}
            disabled={currentSection === 0}
            className="flex items-center space-x-2 px-4 py-2 bg-gray-100 text-gray-600 rounded-lg hover:bg-gray-200 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
          >
            <ChevronRight className="w-4 h-4 rotate-180" />
            <span>Previous</span>
          </button>
          
          <div className="text-center">
            <div className="text-sm text-gray-500 mb-1">Progress</div>
            <div className="w-64 bg-gray-200 rounded-full h-2">
              <div
                className="bg-indigo-600 h-2 rounded-full transition-all duration-300"
                style={{ width: `${((currentSection + 1) / sections.length) * 100}%` }}
              />
            </div>
          </div>

          <button
            onClick={nextSection}
            disabled={currentSection === sections.length - 1}
            className="flex items-center space-x-2 px-4 py-2 bg-indigo-600 text-white rounded-lg hover:bg-indigo-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
          >
            <span>Next</span>
            <ChevronRight className="w-4 h-4" />
          </button>
        </div>
      </div>
    </div>
  );
}