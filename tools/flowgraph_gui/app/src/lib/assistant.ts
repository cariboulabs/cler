export type Role = 'user' | 'assistant';

export type Usage = { input_tokens: number; output_tokens: number };

export type Message = {
  id: number;
  role: Role;
  text: string;
  usage: Usage | null;
  error: string | null;
};

export type Piece = { text: string; code: boolean; strong: boolean };

export type Line = { kind: 'text' | 'bullet' | 'number'; marker: string; pieces: Piece[] };

export type Chip = { label: string; question: string; enabled: boolean; hint: string };

const SPANS = /(`[^`]+`|\*\*[^*]+\*\*)/;
const BULLET = /^\s*[-*+]\s+(.*)$/;
const NUMBER = /^\s*(\d{1,3})[.)]\s+(.*)$/;
const HEADING = /^#{1,6}\s+(.*)$/;

const NO_SELECTION = 'select a block on the canvas first';

export function inline(text: string): Piece[] {
  return text
    .split(SPANS)
    .filter((part) => part.length > 0)
    .map((part) => {
      if (part.startsWith('`') && part.endsWith('`') && part.length > 1) {
        return { text: part.slice(1, -1), code: true, strong: false };
      }
      if (part.startsWith('**') && part.endsWith('**') && part.length > 3) {
        return { text: part.slice(2, -2), code: false, strong: true };
      }
      return { text: part, code: false, strong: false };
    });
}

export function render(text: string): Line[] {
  return text.split('\n').map((raw) => {
    const heading = HEADING.exec(raw);
    if (heading) {
      return { kind: 'text' as const, marker: '', pieces: strong(heading[1] ?? '') };
    }
    const numbered = NUMBER.exec(raw);
    if (numbered) {
      return {
        kind: 'number' as const,
        marker: `${numbered[1]}.`,
        pieces: inline(numbered[2] ?? '')
      };
    }
    const bullet = BULLET.exec(raw);
    if (bullet) {
      return { kind: 'bullet' as const, marker: '•', pieces: inline(bullet[1] ?? '') };
    }
    return { kind: 'text' as const, marker: '', pieces: inline(raw) };
  });
}

function strong(text: string): Piece[] {
  return [{ text, code: false, strong: true }];
}

export function chips(selection: { label: string; var: string } | null): Chip[] {
  return [
    {
      label: 'Explain this flowgraph',
      question: 'Explain this flowgraph: what does it do, block by block?',
      enabled: true,
      hint: ''
    },
    {
      label: 'Why is this edge red?',
      question:
        'One of the edges is drawn red. Which edge is it, why is it a type conflict, and what would fix it?',
      enabled: true,
      hint: ''
    },
    {
      label: selection ? `What does ${selection.label} do?` : 'What does the selected block do?',
      question: selection
        ? `What does the block ${selection.label} (var ${selection.var}) do in this flowgraph, and how is it wired?`
        : '',
      enabled: selection !== null,
      hint: selection ? '' : NO_SELECTION
    }
  ];
}

export function historyOf(messages: Message[]): { role: Role; text: string }[] {
  return messages
    .filter((message) => message.error === null && message.text.trim().length > 0)
    .map((message) => ({ role: message.role, text: message.text }));
}
