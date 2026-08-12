<script lang="ts">
  import { untrack } from 'svelte';
  import { blurAction, keyAction, type Field, type FieldAction, type Outcome } from './inspector';
  import type { Command } from './schema';

  type Props = {
    scope: string;
    fields: Field[];
    ownerReason: string | null;
    enabled: boolean;
    submit: (command: Command) => Promise<Outcome>;
  };

  const { scope, fields, ownerReason, enabled, submit }: Props = $props();

  const IME_KEY_CODE = 229;

  let drafts = $state<Record<string, string>>({});
  let errors = $state<Record<string, string>>({});
  let inFlight = $state<Record<string, string>>({});

  $effect(() => {
    const prefix = scope;
    untrack(() => {
      drafts = within(drafts, prefix);
      errors = within(errors, prefix);
      inFlight = within(inFlight, prefix);
    });
  });

  export function discardDrafts(): void {
    drafts = {};
    errors = {};
  }

  function keyOf(field: Field): string {
    return scope + field.id;
  }

  function omit(map: Record<string, string>, key: string): Record<string, string> {
    return Object.fromEntries(Object.entries(map).filter(([entry]) => entry !== key));
  }

  function within(map: Record<string, string>, prefix: string): Record<string, string> {
    return Object.fromEntries(Object.entries(map).filter(([entry]) => entry.startsWith(prefix)));
  }

  function fieldElement(id: string): HTMLInputElement | null {
    return document.querySelector<HTMLInputElement>(`input[data-field="${id}"]`);
  }

  function restoreFocus(id: string, hadFocus: boolean) {
    if (!hadFocus) return;
    const active = document.activeElement;
    if (active && active !== document.body) return;
    fieldElement(id)?.focus();
  }

  async function commit(field: Field, key: string, text: string) {
    inFlight = { ...inFlight, [key]: text };
    const hadFocus = document.activeElement === fieldElement(field.id);
    const outcome = await submit(field.toCommand(text));
    inFlight = omit(inFlight, key);
    if (!key.startsWith(scope)) return;
    drafts = omit(drafts, key);
    errors = outcome.ok ? omit(errors, key) : { ...errors, [key]: outcome.message };
    restoreFocus(field.id, hadFocus);
  }

  function run(field: Field, action: FieldAction) {
    if (action.kind === 'none') return;
    const key = keyOf(field);
    if (action.kind === 'revert') {
      if (inFlight[key] !== undefined) return;
      drafts = omit(drafts, key);
      errors = omit(errors, key);
      return;
    }
    if (inFlight[key] === action.text) return;
    const refusal = field.refuse?.(action.text) ?? null;
    if (refusal) {
      drafts = omit(drafts, key);
      errors = { ...errors, [key]: refusal };
      return;
    }
    void commit(field, key, action.text);
  }

  function onKeydown(event: KeyboardEvent, field: Field) {
    if (event.isComposing || event.keyCode === IME_KEY_CODE) return;
    run(field, keyAction(event.key, drafts[keyOf(field)], field.value));
  }

  function readable(text: string): string {
    return text.replace(/_/g, ' ');
  }

  function hintText(field: Field): string | undefined {
    if (!field.hint) return undefined;
    return field.hintIsCode ? field.hint : readable(field.hint);
  }
</script>

<div class="fields">
  {#each fields as field (field.id)}
    <label class="field" class:ro={!field.editable}>
      <span class="label"
        >{field.label}{#if field.slot}<span class="slot" data-slot={field.id}>{field.slot}</span
          >{/if}</span
      >
      <input
        type="text"
        data-field={field.id}
        value={drafts[keyOf(field)] ?? field.value}
        disabled={!enabled || !field.editable}
        title={hintText(field)}
        oninput={(event) => (drafts[keyOf(field)] = event.currentTarget.value)}
        onblur={() => run(field, blurAction(drafts[keyOf(field)], field.value))}
        onkeydown={(event) => onKeydown(event, field)}
      />
      {#if errors[keyOf(field)]}
        <span class="err" data-error={field.id}>{errors[keyOf(field)]}</span>
      {:else if field.hint && field.hint !== ownerReason}
        <span class="hint">{hintText(field)}</span>
      {/if}
    </label>
  {/each}
</div>

<style>
  .fields {
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: var(--sp-0);
  }
  .label {
    display: flex;
    gap: var(--sp-2);
    font-size: 11px;
    color: var(--muted);
  }
  .slot {
    margin-left: auto;
    font-family: var(--mono);
    font-size: 11px;
    color: var(--faint);
  }
  input {
    width: 100%;
    background: var(--bg-2);
    color: var(--fg);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: var(--sp-1) var(--sp-2);
    font-family: var(--mono);
    font-size: 11px;
  }
  input:hover:not(:disabled) {
    border-color: var(--border-hi);
  }
  input:focus {
    outline: none;
    border-color: var(--accent-hi);
    box-shadow: var(--glow) color-mix(in srgb, var(--accent) 45%, transparent);
  }
  input:disabled {
    color: var(--muted);
    background: var(--bg-1);
    border-style: dashed;
    cursor: not-allowed;
  }
  .field.ro .label {
    color: var(--muted);
  }
  .hint {
    font-size: 11px;
    color: var(--muted);
  }
  .err {
    font-size: 11px;
    color: var(--danger-fg);
  }
</style>
