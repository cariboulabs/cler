import type { Command } from './schema';

export type Role = 'user' | 'assistant';

export type Usage = { input_tokens: number; output_tokens: number };

export type ProposalState = 'ready' | 'refused' | 'accepted' | 'rejected';

export type DiffLine = { kind: 'add' | 'del' | 'meta' | 'same'; text: string };

export type Proposal = {
  rationale: string;
  commands: Command[];
  baseRevision: number;
  dropped: number;
  diff: DiffLine[];
  splices: number;
  refusal: string | null;
  state: ProposalState;
  appliedAt: number | null;
};

export type Message = {
  id: number;
  role: Role;
  text: string;
  usage: Usage | null;
  error: string | null;
  proposal: Proposal | null;
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

export function diffLines(text: string): DiffLine[] {
  const raw = text.split('\n');
  if (raw.at(-1) === '') raw.pop();
  return raw.map((line) => {
    if (line.startsWith('@@')) return { kind: 'meta' as const, text: line };
    if (line.startsWith('+')) return { kind: 'add' as const, text: line.slice(1) };
    if (line.startsWith('-')) return { kind: 'del' as const, text: line.slice(1) };
    return { kind: 'same' as const, text: line.slice(1) };
  });
}

function portOf(name: string, index: number | null | undefined): string {
  return index === null || index === undefined ? name : `${name}[${index}]`;
}

function typeOf(type: string, args: string[] | undefined): string {
  return args === undefined || args.length === 0 ? type : `${type}<${args.join(', ')}>`;
}

function counted(count: number | undefined, noun: string): string {
  const total = count ?? 0;
  return `${total} ${noun}${total === 1 ? '' : 's'}`;
}

export function describeCommand(command: Command): string {
  switch (command.command) {
    case 'set_param':
      return `set argument ${command.ctor_arg_index} of ${command.block} to ${command.new_text}`;
    case 'set_template_arg':
      return `set template argument ${command.template_arg_index} of ${command.block} to ${command.new_text}`;
    case 'set_display_name':
      return `rename ${command.block} to "${command.new_text}"`;
    case 'set_config':
      return `set ${command.path} to ${command.new_value}`;
    case 'connect':
      return `connect ${command.from} → ${command.to}.${portOf(command.port, command.port_index)}`;
    case 'disconnect':
      return `disconnect edge ${command.edge}`;
    case 'add_block':
      return `add ${typeOf(command.type, command.template_args)} as ${command.var_name}`;
    case 'remove_from_graph':
      return `take ${command.block} out of the graph, keeping its declaration`;
    case 'add_to_graph':
      return `give ${command.block} a runner so it executes`;
    case 'add_render':
      return `draw ${command.block} in the gui loop`;
    case 'remove_render':
      return `stop drawing ${command.block}`;
    case 'delete_block':
      return `delete ${command.block} and its declaration`;
    case 'define_block':
      return `define block ${command.name} with ${counted(command.inputs?.length, 'input')} and ${counted(command.outputs, 'output')}`;
  }
}

export function historyOf(messages: Message[]): { role: Role; text: string }[] {
  return messages
    .filter((message) => message.error === null && message.text.trim().length > 0)
    .map((message) => ({ role: message.role, text: message.text }));
}
