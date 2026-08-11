export type Span = { start: number; end: number };

export type Capability = { editable: boolean; read_only_reason: string | null };

export type TemplateArg = { text: string; resolved: string | null; span: Span };

export type CtorArg = { text: string; span: Span };

export type Block = Capability & {
  var: string;
  type_text: string;
  type_name: string;
  alias: string | null;
  template_args: TemplateArg[];
  ctor_args: CtorArg[];
  display_name: string | null;
  in_graph: boolean;
  span: Span;
};

export type PortKind = 'field' | 'indexed_field' | string;

export type Port = { name: string; index: number | null; kind: PortKind };

export type Edge = Capability & {
  from: string;
  to: string;
  port: Port;
  runner_index: number;
  arg_index: number;
  text: string;
  span: Span;
  sample_type: string | null;
  source_type: string | null;
  type_conflict: boolean;
};

export type Runner = Capability & {
  index: number;
  block: string;
  block_expr: string;
  may_block: boolean;
  form: string;
  span: Span;
};

export type ConfigAssignment = Capability & {
  path: string;
  value: string;
  span: Span;
  value_span: Span;
};

export type SiteConfig = Capability & {
  var: string | null;
  source: string;
  assignments: ConfigAssignment[];
  run_call_span: Span;
};

export type Unresolved = {
  text: string;
  span: Span;
  runner_index: number;
  arg_index: number;
  reason: string;
};

export type Site = Capability & {
  function: string;
  call_offset: number;
  span: Span;
  flowgraph_var: string;
  blocks: Block[];
  edges: Edge[];
  runners: Runner[];
  config: SiteConfig | null;
  unresolved: Unresolved[];
};

export type FileModel = {
  version: string;
  file: string;
  sites: Site[];
  sha256?: string;
  errors?: Span[];
};

export type ParseResult = FileModel;

export type DocumentState = {
  path: string;
  revision: number;
  model: FileModel;
  source: string;
  canUndo: boolean;
  canRedo: boolean;
  externalChange: boolean;
};

export function lineOfOffset(source: string, offset: number): number {
  const upto = source.slice(0, Math.max(0, Math.min(offset, source.length)));
  return upto.split('\n').length;
}

export function lineTextAt(source: string, offset: number): string {
  const at = Math.max(0, Math.min(offset, source.length));
  const start = source.lastIndexOf('\n', at - 1) + 1;
  const end = source.indexOf('\n', at);
  return source.slice(start, end === -1 ? source.length : end).trim();
}

export type Command =
  | { command: 'set_param'; site: number; block: string; ctor_arg_index: number; new_text: string }
  | {
      command: 'set_template_arg';
      site: number;
      block: string;
      template_arg_index: number;
      new_text: string;
    }
  | { command: 'set_display_name'; site: number; block: string; new_text: string }
  | { command: 'set_config'; site: number; path: string; new_value: string }
  | {
      command: 'connect';
      site: number;
      from: string;
      to: string;
      port: string;
      port_index: number | null;
    }
  | { command: 'disconnect'; site: number; edge: number }
  | {
      command: 'add_block';
      site: number;
      type: string;
      template_args: string[];
      ctor_args: string[];
      var_name: string;
    }
  | { command: 'remove_from_graph'; site: number; block: string }
  | { command: 'delete_block'; site: number; block: string }
  | {
      command: 'define_block';
      site: number;
      name: string;
      value_type: string;
      inputs: { name: string }[];
      outputs: number;
      params: { name: string; cpp_type: string; default: string | null }[];
      may_block: boolean;
    };

export function siteViewIds(sites: Site[]): string[] {
  const seen = new Map<string, number>();
  return sites.map((site) => {
    const ordinal = seen.get(site.function) ?? 0;
    seen.set(site.function, ordinal + 1);
    return `${site.function}#${ordinal}`;
  });
}

export function siteLabel(site: Site): string {
  return `${site.function}() — ${site.blocks.length} blocks, ${site.edges.length} edges`;
}
