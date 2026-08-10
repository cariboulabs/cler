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

export type Site = Capability & {
  function: string;
  call_offset: number;
  span: Span;
  flowgraph_var: string;
  blocks: Block[];
  edges: Edge[];
  runners: Runner[];
  config: SiteConfig | null;
  unresolved: unknown[];
};

export type ParseResult = {
  version: string;
  file: string;
  sites: Site[];
};

export function siteId(site: Site): string {
  return `${site.function}@${site.call_offset}`;
}

export function siteLabel(site: Site): string {
  return `${site.function}() — ${site.blocks.length} blocks, ${site.edges.length} edges`;
}
