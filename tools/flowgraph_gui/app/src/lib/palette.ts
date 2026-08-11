import type { Block, Command, Site } from './schema';

export type PortCount =
  | 'unknown'
  | { fixed: number }
  | { template_arg: number }
  | { ctor_arg: number }
  | { ctor_arg_len: number };

export type SpecPort = {
  name: string;
  direction: 'input' | 'output';
  element_type: string;
  variable: boolean;
};

export type TemplateParamKind = 'type' | { non_type: { param_type: string } };

export type TemplateParam = {
  name: string;
  kind: TemplateParamKind;
  default: string | null;
  pack: boolean;
};

export type CtorParam = { name: string; param_type: string; default: string | null };

export type Synonym = { name: string; template_args: string[] };

export type BlockSpec = {
  name: string;
  origin: string;
  synonyms: Synonym[];
  template_params: TemplateParam[];
  ctor_params: CtorParam[];
  may_block: boolean;
  conditional_members: boolean;
  ports: SpecPort[];
  input_count: PortCount;
  output_count: PortCount;
};

export const DRAG_TYPE = 'application/cler-block';

export type Authority =
  | { kind: 'unknown' }
  | { kind: 'fixed'; value: number }
  | { kind: 'template_arg'; index: number }
  | { kind: 'ctor_arg'; index: number }
  | { kind: 'ctor_arg_len'; index: number };

export function authorityOf(count: PortCount): Authority {
  if (count === 'unknown') return { kind: 'unknown' };
  if ('fixed' in count) return { kind: 'fixed', value: count.fixed };
  if ('template_arg' in count) return { kind: 'template_arg', index: count.template_arg };
  if ('ctor_arg' in count) return { kind: 'ctor_arg', index: count.ctor_arg };
  return { kind: 'ctor_arg_len', index: count.ctor_arg_len };
}

function integer(text: string | null | undefined): number | null {
  if (text === null || text === undefined) return null;
  const found = /^\s*(\d+)\s*$/.exec(text);
  return found?.[1] === undefined ? null : Number(found[1]);
}

const OPENERS = '([{<';
const CLOSERS = ')]}>';
const QUOTES = '"\'';

function afterLiteral(text: string, at: number): number {
  const quote = text.charAt(at);
  let index = at + 1;
  while (index < text.length) {
    const character = text.charAt(index);
    if (character === '\\') {
      index += 2;
      continue;
    }
    if (character === quote) return index + 1;
    index += 1;
  }
  return index;
}

export function braceListLength(text: string): number | null {
  const open = text.indexOf('{');
  const close = text.lastIndexOf('}');
  if (open === -1 || close < open) return null;
  const inner = text.slice(open + 1, close);
  if (inner.trim().length === 0) return 0;
  let depth = 0;
  let items = 1;
  let at = 0;
  while (at < inner.length) {
    const character = inner.charAt(at);
    if (QUOTES.includes(character)) {
      at = afterLiteral(inner, at);
      continue;
    }
    if (OPENERS.includes(character)) depth += 1;
    else if (CLOSERS.includes(character)) depth -= 1;
    else if (character === ',' && depth === 0) items += 1;
    at += 1;
  }
  return items;
}

export function countOf(block: Block, count: PortCount): number | null {
  const authority = authorityOf(count);
  switch (authority.kind) {
    case 'fixed':
      return authority.value;
    case 'template_arg': {
      const arg = block.template_args[authority.index];
      return integer(arg?.resolved) ?? integer(arg?.text);
    }
    case 'ctor_arg':
      return integer(block.ctor_args[authority.index]?.text);
    case 'ctor_arg_len': {
      const arg = block.ctor_args[authority.index];
      return arg === undefined ? null : braceListLength(arg.text);
    }
    default:
      return null;
  }
}

export function specFor(specs: BlockSpec[], typeName: string): BlockSpec | undefined {
  return specs.find(
    (spec) =>
      spec.name === typeName || spec.synonyms.some((synonym) => synonym.name === typeName)
  );
}

export function specOfBlock(specs: BlockSpec[], block: Block): BlockSpec | undefined {
  return specFor(specs, block.alias ?? block.type_name);
}

export type RequiredBlockField = {
  kind: 'template' | 'constructor';
  index: number;
  name: string;
};

export const REQUIRED_ARGUMENT_PLACEHOLDER = 'cler::gui::required_field';

export function isRequiredArgumentPlaceholder(text: string | null | undefined): boolean {
  return text?.trim() === REQUIRED_ARGUMENT_PLACEHOLDER;
}

export type InitialBlockArguments = { templateArgs: string[]; ctorArgs: string[] };

export function initialBlockArguments(spec: BlockSpec, varName?: string): InitialBlockArguments {
  return {
    templateArgs: spec.template_params.flatMap((param) => {
      if (param.pack) return [];
      return [param.default ?? REQUIRED_ARGUMENT_PLACEHOLDER];
    }),
    ctorArgs: spec.ctor_params.map((param, index) => {
      if (index === 0 && varName && param.param_type.includes('char*')) return `"${varName}"`;
      return param.default ?? REQUIRED_ARGUMENT_PLACEHOLDER;
    })
  };
}

function missingArgument(text: string | null | undefined): boolean {
  return !text?.trim() || isRequiredArgumentPlaceholder(text);
}

export function missingRequiredFields(block: Block, spec: BlockSpec): RequiredBlockField[] {
  const templates = spec.template_params.flatMap((param, index): RequiredBlockField[] => {
    if (param.default !== null || param.pack) return [];
    const value = block.template_args[index]?.text;
    return missingArgument(value) ? [{ kind: 'template', index, name: param.name }] : [];
  });
  const constructors = spec.ctor_params.flatMap((param, index): RequiredBlockField[] => {
    if (param.default !== null) return [];
    const value = block.ctor_args[index]?.text;
    return missingArgument(value) ? [{ kind: 'constructor', index, name: param.name }] : [];
  });
  return [...templates, ...constructors];
}

export function blockIsValid(block: Block, spec: BlockSpec | undefined): boolean {
  return spec === undefined || missingRequiredFields(block, spec).length === 0;
}

export function categoryOf(spec: BlockSpec, documentPath: string): string {
  if (spec.origin === documentPath) return 'this file';
  const parts = spec.origin.split(/[\\/]/);
  return parts[parts.length - 2] ?? 'blocks';
}

function countLabel(count: PortCount, ports: SpecPort[]): string {
  const authority = authorityOf(count);
  if (authority.kind === 'fixed') return String(authority.value);
  if (authority.kind === 'unknown') return ports.length === 0 ? '0' : '?';
  return 'n';
}

export function portsSummary(spec: BlockSpec): string {
  const inputs = spec.ports.filter((port) => port.direction === 'input');
  const outputs = spec.ports.filter((port) => port.direction === 'output');
  const left = countLabel(spec.input_count, inputs);
  const right = countLabel(spec.output_count, outputs);
  if (left === '?' || right === '?') return '';
  return `${left} in · ${right} out`;
}

function templateHead(spec: BlockSpec): string {
  if (spec.template_params.length === 0) return spec.name;
  return `${spec.name}<${spec.template_params.map((param) => param.name).join(', ')}>`;
}

export function ctorSignature(spec: BlockSpec): string {
  const params = spec.ctor_params.map((param) => {
    const tail = param.default === null ? '' : ` = ${param.default}`;
    return `${param.param_type} ${param.name}${tail}`;
  });
  return `${templateHead(spec)}(${params.join(', ')})`;
}

export function searchSpecs(
  specs: BlockSpec[],
  query: string,
  documentPath: string
): BlockSpec[] {
  const needle = query.trim().toLowerCase();
  const matches = specs.filter((spec) => {
    if (needle.length === 0) return true;
    const haystack = [
      spec.name,
      categoryOf(spec, documentPath),
      ...spec.synonyms.map((synonym) => synonym.name),
      ...spec.ports.map((port) => port.element_type)
    ]
      .join(' ')
      .toLowerCase();
    return haystack.includes(needle);
  });
  return [...matches].sort((left, right) => {
    const byCategory = categoryOf(left, documentPath).localeCompare(
      categoryOf(right, documentPath),
      'en'
    );
    return byCategory === 0 ? left.name.localeCompare(right.name, 'en') : byCategory;
  });
}

export function specsFromSites(sites: Site[], documentPath: string): BlockSpec[] {
  const found = new Map<string, BlockSpec>();
  for (const site of sites) {
    for (const block of site.blocks) {
      const spec = found.get(block.type_name) ?? {
        name: block.type_name,
        origin: documentPath,
        synonyms: [],
        template_params: [],
        ctor_params: [],
        may_block: false,
        conditional_members: false,
        ports: [],
        input_count: 'unknown' as PortCount,
        output_count: 'unknown' as PortCount
      };
      for (const edge of site.edges) {
        if (edge.to === block.var && !spec.ports.some((port) => port.name === edge.port.name)) {
          spec.ports.push({
            name: edge.port.name,
            direction: 'input',
            element_type: edge.sample_type ?? '?',
            variable: edge.port.index !== null
          });
        }
      }
      if (
        site.edges.some((edge) => edge.from === block.var) &&
        !spec.ports.some((port) => port.direction === 'output')
      ) {
        spec.ports.push({ name: 'out', direction: 'output', element_type: '?', variable: false });
      }
      spec.may_block ||= site.runners.some(
        (runner) => runner.block === block.var && runner.may_block
      );
      found.set(block.type_name, spec);
    }
  }
  return [...found.values()];
}

const IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;

function initials(typeName: string): string {
  const trimmed = typeName.replace(/Block$/, '');
  const words = trimmed.match(/[A-Z][a-z0-9]*|[a-z0-9]+/g) ?? [trimmed];
  return words.join('_').toLowerCase() || 'block';
}

export function suggestVarName(typeName: string, taken: string[]): string {
  const base = IDENTIFIER.test(initials(typeName)) ? initials(typeName) : 'block';
  if (!taken.includes(base)) return base;
  let ordinal = 2;
  while (taken.includes(`${base}${ordinal}`)) ordinal += 1;
  return `${base}${ordinal}`;
}

export type AddField = {
  id: string;
  label: string;
  value: string;
  hint: string;
  required: boolean;
};

export type AddForm = {
  varName: string;
  templateArgs: AddField[];
  ctorArgs: AddField[];
};

function displayNameFor(varName: string): string {
  return `"${varName}"`;
}

export function addForm(spec: BlockSpec, varName: string): AddForm {
  return {
    varName,
    templateArgs: spec.template_params.map((param, index) => ({
      id: `template.${index}`,
      label: param.name,
      value: param.default ?? '',
      hint: typeof param.kind === 'string' ? 'type' : param.kind.non_type.param_type,
      required: param.default === null && !param.pack
    })),
    ctorArgs: spec.ctor_params.map((param, index) => ({
      id: `ctor.${index}`,
      label: param.name,
      value: index === 0 && param.param_type.includes('char*') ? displayNameFor(varName) : (param.default ?? ''),
      hint: param.param_type,
      required: param.default === null
    }))
  };
}

export function renameInForm(form: AddForm, varName: string): AddForm {
  const first = form.ctorArgs[0];
  const renamed =
    first && first.value === displayNameFor(form.varName)
      ? [{ ...first, value: displayNameFor(varName) }, ...form.ctorArgs.slice(1)]
      : form.ctorArgs;
  return { ...form, varName, ctorArgs: renamed };
}

export type AddGap = { field: string; message: string };

const REQUIRED_GAP = 'required';
const POSITIONAL_GAP = 'fill this in — a later argument is set';

function fieldGaps(fields: AddField[]): AddGap[] {
  const filled = fields.map((field) => field.value.trim().length > 0);
  return fields.flatMap((field, index) => {
    if (filled[index]) return [];
    if (field.required) return [{ field: field.id, message: REQUIRED_GAP }];
    const later = filled.slice(index + 1).some((set) => set);
    return later ? [{ field: field.id, message: POSITIONAL_GAP }] : [];
  });
}

export function addGaps(form: AddForm): AddGap[] {
  const named =
    form.varName.trim().length === 0 ? [{ field: 'var_name', message: REQUIRED_GAP }] : [];
  return [...named, ...fieldGaps(form.templateArgs), ...fieldGaps(form.ctorArgs)];
}

function positional(fields: AddField[]): string[] {
  const values = fields.map((field) => field.value.trim());
  let last = values.length;
  while (last > 0 && values[last - 1] === '') last -= 1;
  return values.slice(0, last);
}

export function addBlockCommand(site: number, spec: BlockSpec, form: AddForm): Command {
  return {
    command: 'add_block',
    site,
    type: spec.name,
    template_args: positional(form.templateArgs),
    ctor_args: positional(form.ctorArgs),
    var_name: form.varName.trim()
  };
}

export type FieldRefusal = { field: string | null; message: string };

function refusalText(record: Record<string, unknown>): string {
  const text = typeof record.text === 'string' ? record.text : '';
  switch (record.error) {
    case 'invalid_identifier':
      return `"${text}" is not a valid C++ identifier`;
    case 'reserved_identifier':
      return `"${text}" is a C++ keyword`;
    case 'duplicate_variable':
      return `${String(record.var_name)} is already declared in this function`;
    case 'invalid_expression':
      return `"${text}" is not a valid ${String(record.element) === 'template_args' ? 'template argument' : 'expression'}`;
    case 'empty_constructor_arguments':
      return 'this block needs at least one constructor argument';
    default:
      return '';
  }
}

export function addRefusal(
  record: Record<string, unknown> | null,
  form: AddForm
): FieldRefusal | null {
  if (!record) return null;
  const message = refusalText(record);
  if (message.length === 0) return null;
  if (record.error === 'invalid_expression') {
    const pool = record.element === 'template_args' ? form.templateArgs : form.ctorArgs;
    const hit = pool.find((field) => field.value.trim() === record.text);
    return { field: hit?.id ?? null, message };
  }
  if (record.error === 'invalid_identifier' && record.text !== form.varName.trim()) {
    return { field: null, message: `"${String(record.text)}" is not a valid block type` };
  }
  if (record.error === 'empty_constructor_arguments') return { field: null, message };
  return { field: 'var_name', message };
}

export type Wire = { from: string; to: string; port: string; portIndex: number | null };

export type ConnectPlan = { commands: Command[] } | { refusal: string };

function blockOf(site: Site, name: string): Block | undefined {
  return site.blocks.find((candidate) => candidate.var === name);
}

function growCommand(
  siteIndex: number,
  block: Block,
  authority: Authority,
  required: number
): Command | null {
  if (authority.kind === 'template_arg') {
    return {
      command: 'set_template_arg',
      site: siteIndex,
      block: block.var,
      template_arg_index: authority.index,
      new_text: String(required)
    };
  }
  if (authority.kind === 'ctor_arg') {
    return {
      command: 'set_param',
      site: siteIndex,
      block: block.var,
      ctor_arg_index: authority.index,
      new_text: String(required)
    };
  }
  return null;
}

function labelRefusal(spec: BlockSpec, authority: Authority, current: number): string {
  const name =
    authority.kind === 'ctor_arg_len'
      ? (spec.ctor_params[authority.index]?.name ?? 'label list')
      : 'label list';
  return `${spec.name} sizes its ports from ${name}, which holds ${current} — add a label there first, then wire it`;
}

type Growth = { commands: Command[] } | { refusal: string } | null;

function grow(
  siteIndex: number,
  block: Block,
  spec: BlockSpec | undefined,
  count: PortCount | undefined,
  required: number,
  what: 'input' | 'output'
): Growth {
  if (!spec || count === undefined) return null;
  const authority = authorityOf(count);
  if (authority.kind === 'unknown') return null;
  const current = countOf(block, count);
  if (current === null || required <= current) return null;
  if (authority.kind === 'fixed') {
    return {
      refusal: `${spec.name} has ${current} ${what}${current === 1 ? '' : 's'} — it cannot take another`
    };
  }
  if (authority.kind === 'ctor_arg_len') {
    return { refusal: labelRefusal(spec, authority, current) };
  }
  const command = growCommand(siteIndex, block, authority, required);
  if (!command) return null;
  if (!block.editable) {
    return { refusal: `${block.var} is read-only, so its ${what} count cannot grow` };
  }
  return { commands: [command] };
}

export function connectPlan(
  siteIndex: number,
  site: Site,
  specs: BlockSpec[],
  wire: Wire,
  moving: number | null = null
): ConnectPlan {
  if (wire.from === wire.to) return { refusal: 'a block cannot wire into itself' };
  const source = blockOf(site, wire.from);
  const target = blockOf(site, wire.to);
  if (!source || !target) return { refusal: 'that wire has an endpoint outside this graph' };
  for (const block of [source, target]) {
    if (!block.editable) {
      return {
        refusal: `${block.var} is read-only: ${(block.read_only_reason ?? 'unsupported form').replace(/_/g, ' ')}`
      };
    }
  }
  const others = site.edges.filter((_, index) => index !== moving);
  const taken = others.find(
    (edge) =>
      edge.to === wire.to && edge.port.name === wire.port && edge.port.index === wire.portIndex
  );
  if (taken) {
    const slot = `${wire.to}.${wire.port}${wire.portIndex === null ? '' : `[${wire.portIndex}]`}`;
    return {
      refusal:
        taken.from === wire.from ? 'that wire already exists' : `${slot} already takes ${taken.from}`
    };
  }

  const commands: Command[] = [];
  const targetSpec = specOfBlock(specs, target);
  const inbound =
    wire.portIndex === null
      ? null
      : grow(siteIndex, target, targetSpec, targetSpec?.input_count, wire.portIndex + 1, 'input');
  if (inbound && 'refusal' in inbound) return inbound;
  if (inbound) commands.push(...inbound.commands);

  const sourceSpec = specOfBlock(specs, source);
  const outgoing = others.filter((edge) => edge.from === wire.from).length;
  const outbound = grow(
    siteIndex,
    source,
    sourceSpec,
    sourceSpec?.output_count,
    outgoing + 1,
    'output'
  );
  if (outbound && 'refusal' in outbound) return outbound;
  if (outbound) commands.push(...outbound.commands);

  commands.push({
    command: 'connect',
    site: siteIndex,
    from: wire.from,
    to: wire.to,
    port: wire.port,
    port_index: wire.portIndex
  });
  return { commands };
}

export function reconnectPlan(
  siteIndex: number,
  site: Site,
  specs: BlockSpec[],
  edgeIndex: number,
  wire: Wire
): ConnectPlan {
  const moved = site.edges[edgeIndex];
  if (!moved) return { refusal: 'that wire is no longer in the graph' };
  if (!moved.editable) {
    return {
      refusal: `that wire is read-only: ${(moved.read_only_reason ?? 'unsupported form').replace(/_/g, ' ')}`
    };
  }
  if (
    moved.from === wire.from &&
    moved.to === wire.to &&
    moved.port.name === wire.port &&
    moved.port.index === wire.portIndex
  ) {
    return { commands: [] };
  }
  const plan = connectPlan(siteIndex, site, specs, wire, edgeIndex);
  if ('refusal' in plan) return plan;
  return {
    commands: [{ command: 'disconnect', site: siteIndex, edge: edgeIndex }, ...plan.commands]
  };
}
