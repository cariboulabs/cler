import { authorityOf, countOf, type BlockSpec, type PortCount } from './palette';
import type { Block, Command, Site, SiteConfig } from './schema';

export type Field = {
  id: string;
  label: string;
  slot: string;
  value: string;
  hint: string | null;
  editable: boolean;
  toCommand: (text: string) => Command;
  refuse?: (text: string) => string | null;
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

type Slot = { kind: 'template' | 'ctor'; index: number };

function wiredInputs(site: Site, blockVar: string): number {
  let needed = 0;
  for (const edge of site.edges) {
    if (edge.to !== blockVar || edge.port.index === null) continue;
    needed = Math.max(needed, edge.port.index + 1);
  }
  return needed;
}

function wiredOutputs(site: Site, blockVar: string): number {
  return site.edges.filter((edge) => edge.from === blockVar).length;
}

function governs(count: PortCount, slot: Slot): boolean {
  const authority = authorityOf(count);
  if (authority.kind === 'template_arg') {
    return slot.kind === 'template' && authority.index === slot.index;
  }
  if (authority.kind === 'ctor_arg' || authority.kind === 'ctor_arg_len') {
    return slot.kind === 'ctor' && authority.index === slot.index;
  }
  return false;
}

function rewritten(block: Block, slot: Slot, text: string): Block {
  if (slot.kind === 'template') {
    return {
      ...block,
      template_args: block.template_args.map((arg, at) =>
        at === slot.index ? { ...arg, text, resolved: null } : arg
      )
    };
  }
  return {
    ...block,
    ctor_args: block.ctor_args.map((arg, at) => (at === slot.index ? { ...arg, text } : arg))
  };
}

function shrinkGuard(
  block: Block,
  slot: Slot,
  spec: BlockSpec | undefined,
  site: Site | undefined
): ((text: string) => string | null) | undefined {
  if (!spec || !site) return undefined;
  const groups: [PortCount, number][] = [
    [spec.input_count, wiredInputs(site, block.var)],
    [spec.output_count, wiredOutputs(site, block.var)]
  ];
  const governing = groups.filter(([count]) => governs(count, slot));
  if (governing.length === 0) return undefined;
  return (text) => {
    for (const [count, wired] of governing) {
      const next = countOf(rewritten(block, slot, text), count);
      if (next !== null && next < wired) {
        return `${wired} ports are wired — disconnect them before lowering this`;
      }
    }
    return null;
  };
}

export function blockFields(
  siteIndex: number,
  block: Block,
  spec?: BlockSpec,
  site?: Site
): Field[] {
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
      site: siteIndex,
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
        site: siteIndex,
        block: block.var,
        template_arg_index: index,
        new_text: text
      }),
      refuse: shrinkGuard(block, { kind: 'template', index }, spec, site)
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
        site: siteIndex,
        block: block.var,
        ctor_arg_index: index,
        new_text: text
      }),
      refuse: shrinkGuard(block, { kind: 'ctor', index }, spec, site)
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
