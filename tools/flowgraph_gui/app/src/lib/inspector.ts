import type { BlockSpec } from './palette';
import type { Block, Command, SiteConfig } from './schema';

export type Field = {
  id: string;
  label: string;
  slot: string;
  value: string;
  hint: string | null;
  editable: boolean;
  toCommand: (text: string) => Command;
};

export type FieldAction = { kind: 'commit'; text: string } | { kind: 'revert' } | { kind: 'none' };

export type Outcome =
  | { ok: true }
  | { ok: false; message: string; record: Record<string, unknown> | null };

const NO_DISPLAY_NAME = 'this block has no display-name argument';

export function blurAction(draft: string | undefined, committed: string): FieldAction {
  if (draft === undefined || draft === committed) return { kind: 'revert' };
  return { kind: 'commit', text: draft };
}

export function keyAction(key: string, draft: string | undefined, committed: string): FieldAction {
  if (key === 'Enter') return blurAction(draft, committed);
  if (key === 'Escape') return { kind: 'revert' };
  return { kind: 'none' };
}

type Naming = { label: string; slot: string; hint: string | null };

function named(
  kind: 'template' | 'ctor',
  index: number,
  names: { name: string; type: string | null }[] | null
): Naming {
  const found = names?.[index];
  if (!found) return { label: `${kind} arg ${index}`, slot: '', hint: null };
  return { label: found.name, slot: `${kind} ${index}`, hint: found.type };
}

function ctorNames(block: Block, spec: BlockSpec | undefined) {
  if (!spec || spec.ctor_params.length !== block.ctor_args.length) return null;
  return spec.ctor_params.map((param) => ({ name: param.name, type: param.param_type }));
}

function templateNames(block: Block, spec: BlockSpec | undefined) {
  if (!spec || spec.template_params.length !== block.template_args.length) return null;
  return spec.template_params.map((param) => ({
    name: param.name,
    type: typeof param.kind === 'string' ? null : param.kind.non_type.param_type
  }));
}

export function blockFields(site: number, block: Block, spec?: BlockSpec): Field[] {
  const reason = block.read_only_reason;
  const nameEditable = block.editable && block.display_name !== null;
  const templateLabels = templateNames(block, spec);
  const ctorLabels = ctorNames(block, spec);

  const name: Field = {
    id: `${block.var}.display_name`,
    label: 'display name',
    slot: '',
    value: block.display_name ?? '',
    hint: nameEditable ? null : (reason ?? NO_DISPLAY_NAME),
    editable: nameEditable,
    toCommand: (text) => ({
      command: 'set_display_name',
      site,
      block: block.var,
      new_text: text
    })
  };

  const templates: Field[] = block.template_args.map((arg, index) => {
    const naming = named('template', index, templateLabels);
    return {
      id: `${block.var}.template.${index}`,
      label: naming.label,
      slot: naming.slot,
      value: arg.text,
      hint: block.editable
        ? arg.resolved && arg.resolved !== arg.text
          ? `resolves to ${arg.resolved}`
          : naming.hint
        : reason,
      editable: block.editable,
      toCommand: (text) => ({
        command: 'set_template_arg',
        site,
        block: block.var,
        template_arg_index: index,
        new_text: text
      })
    };
  });

  const ctors: Field[] = block.ctor_args.map((arg, index) => {
    const naming = named('ctor', index, ctorLabels);
    return {
      id: `${block.var}.ctor.${index}`,
      label: naming.label,
      slot: naming.slot,
      value: arg.text,
      hint: block.editable ? naming.hint : reason,
      editable: block.editable,
      toCommand: (text) => ({
        command: 'set_param',
        site,
        block: block.var,
        ctor_arg_index: index,
        new_text: text
      })
    };
  });

  return [name, ...templates, ...ctors];
}

export function configFields(site: number, config: SiteConfig): Field[] {
  return config.assignments.map((assignment) => {
    const editable = config.editable && assignment.editable;
    return {
      id: `config.${assignment.path}`,
      label: assignment.path,
      slot: '',
      value: assignment.value,
      hint: editable ? null : (assignment.read_only_reason ?? config.read_only_reason),
      editable,
      toCommand: (text) => ({
        command: 'set_config',
        site,
        path: assignment.path,
        new_value: text
      })
    };
  });
}
