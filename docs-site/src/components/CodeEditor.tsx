'use client';

import { Editor } from '@monaco-editor/react';
import { useState } from 'react';

interface CodeEditorProps {
  code: string;
  language: string;
  readOnly?: boolean;
  onCodeChange?: (code: string) => void;
  height?: string;
}

export default function CodeEditor({ 
  code, 
  language, 
  readOnly = false, 
  onCodeChange,
  height = '400px'
}: CodeEditorProps) {
  const [editorCode, setEditorCode] = useState(code);

  const handleEditorChange = (value: string | undefined) => {
    if (value !== undefined) {
      setEditorCode(value);
      if (onCodeChange) {
        onCodeChange(value);
      }
    }
  };

  return (
    <div className="border rounded-lg overflow-hidden">
      <div className="bg-gray-800 text-white px-4 py-2 text-sm font-medium">
        {language.toUpperCase()} Code
      </div>
      <Editor
        height={height}
        language={language}
        value={editorCode}
        onChange={handleEditorChange}
        options={{
          readOnly,
          minimap: { enabled: false },
          fontSize: 14,
          lineNumbers: 'on',
          scrollBeyondLastLine: false,
          wordWrap: 'on',
          theme: 'vs-light',
          automaticLayout: true,
          tabSize: 2,
          insertSpaces: true,
        }}
      />
    </div>
  );
}