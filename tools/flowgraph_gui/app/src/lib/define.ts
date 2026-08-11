import { categoryOf, type BlockSpec, type FieldRefusal } from './palette';
import type { Command, Span } from './schema';

export type DefineInput = { name: string };

export type DefineParam = { name: string; cppType: string; default: string };

export type DefineForm = {
  name: string;
  valueType: string;
  inputs: DefineInput[];
  outputs: number;
  params: DefineParam[];
  mayBlock: boolean;
};

export type DefinePreview = { ports: string; ctor: string; note: string };

export const BLOCK_SUFFIX = 'Block';
export const MAX_OUTPUTS = 8;

const IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;
const GENERATED_OUTPUT = /^out\d+$/;
const REQUIRED = 'required';
const SUFFIX_GAP = `a block type name must end in "${BLOCK_SUFFIX}"`;

export function emptyDefineForm(): DefineForm {
  return {
    name: '',
    valueType: 'float',
    inputs: [{ name: 'in' }],
    outputs: 1,
    params: [],
    mayBlock: false
  };
}

export function inputField(index: number): string {
  return `input.${index}`;
}

export function paramField(index: number, part: 'name' | 'type' | 'default'): string {
  return `param.${index}.${part}`;
}

export function shapeMessage(name: string): string {
  return `${name} declares no inputs and no outputs`;
}

export function paletteTypes(specs: BlockSpec[]): string[] {
  const found = new Set<string>();
  for (const spec of specs) {
    const parameters = new Set(spec.template_params.map((param) => param.name));
    for (const port of spec.ports) {
      if (!parameters.has(port.element_type)) found.add(port.element_type);
    }
  }
  return [...found].sort((left, right) => left.localeCompare(right, 'en'));
}

function identifierGap(text: string, field: string): FieldRefusal | null {
  if (text.length === 0) return { field, message: REQUIRED };
  if (!IDENTIFIER.test(text)) {
    return { field, message: `"${text}" is not a valid C++ identifier` };
  }
  if (text.startsWith('_')) {
    return { field, message: `"${text}" becomes a reserved identifier as a member` };
  }
  return null;
}

function takenMessage(text: string): string {
  if (text === 'name') return '"name" is the generated display-name parameter';
  if (GENERATED_OUTPUT.test(text)) return `"${text}" is a generated output parameter`;
  return `"${text}" is already used by another port or parameter`;
}

function members(form: DefineForm): [string, string][] {
  return [
    ...form.inputs.map((input, index): [string, string] => [input.name.trim(), inputField(index)]),
    ...form.params.map((param, index): [string, string] => [
      param.name.trim(),
      paramField(index, 'name')
    ])
  ];
}

function memberGaps(form: DefineForm): FieldRefusal[] {
  const taken = new Set<string>(['name']);
  for (let index = 0; index < form.outputs; index += 1) taken.add(`out${index}`);
  const gaps: FieldRefusal[] = [];
  for (const [text, field] of members(form)) {
    const gap = identifierGap(text, field);
    if (gap) {
      gaps.push(gap);
      continue;
    }
    if (taken.has(text)) {
      gaps.push({ field, message: takenMessage(text) });
      continue;
    }
    taken.add(text);
  }
  return gaps;
}

export function nameGap(
  form: DefineForm,
  specs: BlockSpec[],
  documentPath: string
): FieldRefusal | null {
  const name = form.name.trim();
  if (name.length === 0) return { field: 'name', message: REQUIRED };
  if (!IDENTIFIER.test(name)) {
    return { field: 'name', message: `"${name}" is not a valid C++ identifier` };
  }
  if (name.length <= BLOCK_SUFFIX.length || !name.endsWith(BLOCK_SUFFIX)) {
    return { field: 'name', message: SUFFIX_GAP };
  }
  const clash = specs.find((spec) => spec.name === name);
  if (clash) {
    return {
      field: 'name',
      message: `${name} already exists in ${categoryOf(clash, documentPath)}`
    };
  }
  return null;
}

export function defineGaps(
  form: DefineForm,
  specs: BlockSpec[],
  documentPath: string
): FieldRefusal[] {
  const gaps: FieldRefusal[] = [];
  const name = nameGap(form, specs, documentPath);
  if (name) gaps.push(name);
  if (form.valueType.trim().length === 0) {
    gaps.push({ field: 'value_type', message: REQUIRED });
  }
  if (form.inputs.length === 0 && form.outputs === 0) {
    gaps.push({ field: 'outputs', message: shapeMessage(form.name.trim() || 'this block') });
  }
  gaps.push(...memberGaps(form));
  for (const [index, param] of form.params.entries()) {
    if (param.cppType.trim().length === 0) {
      gaps.push({ field: paramField(index, 'type'), message: REQUIRED });
    }
  }
  return gaps;
}

function listed(names: string[]): string {
  return names.length === 0 ? 'none' : names.join(', ');
}

export function definePreview(form: DefineForm): DefinePreview {
  const name = form.name.trim() || 'NewBlock';
  const value = form.valueType.trim() || 'T';
  const inputs = form.inputs.map((input, index) => input.name.trim() || `in${index}`);
  const outputs = Array.from({ length: form.outputs }, (_, index) => `out${index}`);
  const params = form.params.map((param) => {
    const tail = param.default.trim().length === 0 ? '' : ` = ${param.default.trim()}`;
    return `, ${param.cppType.trim() || 'T'} ${param.name.trim() || 'value'}${tail}`;
  });
  return {
    ports: `${listed(inputs)} → ${listed(outputs)} · ${value}`,
    ctor: `${name}(const char* name${params.join('')})`,
    note: `${form.mayBlock ? 'may_block, ' : ''}procedure() scaffolded with a TODO body`
  };
}

export function defineCommand(site: number, form: DefineForm): Command {
  return {
    command: 'define_block',
    site,
    name: form.name.trim(),
    value_type: form.valueType.trim(),
    inputs: form.inputs.map((input) => ({ name: input.name.trim() })),
    outputs: form.outputs,
    params: form.params.map((param) => ({
      name: param.name.trim(),
      cpp_type: param.cppType.trim(),
      default: param.default.trim().length === 0 ? null : param.default.trim()
    })),
    may_block: form.mayBlock
  };
}

function memberFieldOf(form: DefineForm, text: string): string | null {
  return members(form).find(([name]) => name === text)?.[1] ?? null;
}

function typeFieldOf(form: DefineForm, text: string): string | null {
  const at = form.params.findIndex((param) => param.cppType.trim() === text);
  return at === -1 ? null : paramField(at, 'type');
}

function defaultFieldOf(form: DefineForm, text: string): string | null {
  const at = form.params.findIndex((param) => param.default.trim() === text);
  return at === -1 ? null : paramField(at, 'default');
}

export function defineRefusal(
  record: Record<string, unknown> | null,
  form: DefineForm
): FieldRefusal | null {
  if (!record) return null;
  const text = typeof record.text === 'string' ? record.text : '';
  switch (record.error) {
    case 'invalid_block_name':
      return { field: 'name', message: SUFFIX_GAP };
    case 'duplicate_type':
      return {
        field: 'name',
        message: `${String(record.name)} is already a type in this file`
      };
    case 'invalid_identifier':
      return {
        field: memberFieldOf(form, text) ?? 'name',
        message: `"${text}" is not a valid C++ identifier`
      };
    case 'reserved_identifier':
      return {
        field: memberFieldOf(form, text) ?? 'name',
        message: text.startsWith('_')
          ? `"${text}" becomes a reserved identifier as a member`
          : `"${text}" is a C++ keyword`
      };
    case 'duplicate_variable': {
      const name = String(record.var_name);
      return { field: memberFieldOf(form, name) ?? null, message: takenMessage(name) };
    }
    case 'invalid_type':
      return {
        field: record.element === 'value_type' ? 'value_type' : typeFieldOf(form, text),
        message: `"${text}" is not a valid C++ type`
      };
    case 'invalid_expression':
      return {
        field: defaultFieldOf(form, text),
        message: `"${text}" is not a valid expression`
      };
    case 'unsupported_shape':
      return { field: 'outputs', message: String(record.detail) };
    default:
      return null;
  }
}

export function structSpan(source: string, name: string): Span | null {
  const needle = `struct ${name}`;
  const at = source.indexOf(needle);
  return at === -1 ? null : { start: at, end: at + needle.length };
}
