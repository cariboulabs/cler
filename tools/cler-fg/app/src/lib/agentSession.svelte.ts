import { diffLines, historyOf, type Message, type Proposal, type Usage } from './agent';
import {
  aiAgentAsk,
  aiAgentModels,
  aiAgentOauthLogout,
  aiAgentOauthStart,
  aiAgentStatus,
  aiAgentStop,
  describeApplyError,
  errorRecord,
  inTauri,
  onAiAgentAuthChanged,
  onAiAgentDelta,
  onAiAgentDone,
  onAiAgentProposal,
  previewCommands,
  setAppSettings,
  spansOf,
  type AiAgentProposal,
  type AiAgentStatus,
  type AppSettings,
  type ListedModel
} from './backend';
import type { Outcome } from './inspector';
import type { Command, Span } from './schema';

const NO_SHELL = 'the AI agent is only supported in the desktop app — not in the browser';
const MOVED_ON = 'the graph moved on since that proposal — re-check it before applying';

function unavailable(reason: string): AiAgentStatus {
  return { available: false, provider: 'anthropic', model: '—', reason, method: null };
}

export type AgentDeps = {
  readonly path: string;
  readonly revision: number;
  readonly editable: boolean;
  readonly selected: string | null;
  readonly note: string;
  readonly settings: AppSettings;
  announce: (message: string) => void;
  apply: (commands: Command[]) => Promise<Outcome>;
  onSettings: (next: AppSettings) => void;
  onRefusal: (refusal: { block: string; spans: Span[] }) => void;
};

export class AgentSession {
  status = $state.raw<AiAgentStatus | null>(null);
  messages = $state.raw<Message[]>([]);
  pending = $state<number | null>(null);
  models = $state<ListedModel[]>([]);

  readonly #deps: AgentDeps;
  // the browser build installs __TAURI_INTERNALS__ itself; the agent still needs a real shell
  readonly desktop = inTauri() && !import.meta.env.VITE_CLER_WASM;
  #turns = 0;

  constructor(deps: AgentDeps) {
    this.#deps = deps;
  }

  get enabled(): boolean {
    return this.#deps.editable;
  }

  get note(): string {
    return this.#deps.note;
  }

  get revision(): number {
    return this.#deps.revision;
  }

  get selected(): string | null {
    return this.#deps.selected;
  }

  reset() {
    this.messages = [];
    this.pending = null;
  }

  listen(): (() => void) | undefined {
    if (!this.desktop) return;
    const deltas = onAiAgentDelta((payload) => {
      if (payload.path !== this.#deps.path || this.pending === null) return;
      const target = this.pending;
      this.messages = this.messages.map((message) =>
        message.id === target ? { ...message, text: message.text + payload.text } : message
      );
    });
    const ends = onAiAgentDone((payload) => {
      if (payload.path !== this.#deps.path) return;
      this.#closeReply(payload.usage, payload.error);
    });
    const plans = onAiAgentProposal((payload) => void this.#attachProposal(payload));
    const auth = onAiAgentAuthChanged((next) => {
      this.status = next;
    });
    return () => {
      void Promise.all([deltas, ends, plans, auth]).then((listeners) => {
        for (const unlisten of listeners) unlisten();
      });
    };
  }

  async refreshStatus() {
    if (!this.desktop) {
      this.status = unavailable(NO_SHELL);
      return;
    }
    try {
      this.status = await aiAgentStatus();
    } catch (error) {
      this.status = unavailable(describeApplyError(error));
    }
  }

  // Asked for when the panel is first opened, not at startup: it is a network call for
  // a rail tab most sessions never reach.
  async refreshModels() {
    if (!this.desktop) {
      this.models = [];
      return;
    }
    try {
      this.models = await aiAgentModels();
    } catch {
      this.models = [];
    }
  }

  async setModel(model: string) {
    try {
      this.#deps.onSettings(await setAppSettings({ ...this.#deps.settings, aiAgentModel: model }));
      await this.refreshStatus();
    } catch (error) {
      this.#deps.announce(describeApplyError(error));
    }
  }

  async setProvider(provider: string) {
    if (provider === this.#deps.settings.aiAgentProvider) return;
    this.stop();
    try {
      this.#deps.onSettings(
        await setAppSettings({
          ...this.#deps.settings,
          aiAgentProvider: provider,
          aiAgentModel: null
        })
      );
      await this.refreshStatus();
      await this.refreshModels();
    } catch (error) {
      this.#deps.announce(describeApplyError(error));
    }
  }

  // true while the browser holds the login; failures go to the toast
  async signIn(): Promise<boolean> {
    if (!this.desktop) return false;
    try {
      await aiAgentOauthStart();
      return true;
    } catch (error) {
      this.#deps.announce(describeApplyError(error));
      return false;
    }
  }

  async signOut() {
    if (!this.desktop) return;
    try {
      this.status = await aiAgentOauthLogout();
    } catch (error) {
      this.status = unavailable(describeApplyError(error));
    }
    this.pending = null;
  }

  stop() {
    this.pending = null;
    void aiAgentStop(this.#deps.path).catch(() => undefined);
  }

  async ask(question: string) {
    if (!this.#deps.editable) {
      this.#deps.announce(this.#deps.note);
      return;
    }
    if (this.pending !== null) return;
    const history = historyOf(this.messages);
    const asked: Message = {
      id: ++this.#turns,
      role: 'user',
      text: question,
      usage: null,
      error: null,
      proposal: null
    };
    const reply: Message = {
      id: ++this.#turns,
      role: 'assistant',
      text: '',
      usage: null,
      error: null,
      proposal: null
    };
    this.messages = [...this.messages, asked, reply];
    this.pending = reply.id;
    try {
      await aiAgentAsk(this.#deps.path, question, history, this.#deps.selected);
    } catch (error) {
      this.#closeReply(null, describeApplyError(error));
    }
  }

  retry(id: number) {
    const at = this.messages.findIndex((message) => message.id === id);
    const asked = at > 0 ? this.messages[at - 1] : undefined;
    if (!asked || asked.role !== 'user') return;
    this.messages = this.messages.slice(0, at - 1);
    void this.ask(asked.text);
  }

  async accept(id: number) {
    const plan = this.#planOf(id);
    if (!plan || plan.state !== 'ready') return;
    if (plan.baseRevision !== this.#deps.revision) {
      this.#deps.announce(MOVED_ON);
      return;
    }
    const outcome = await this.#deps.apply(plan.commands);
    const settled = this.#planOf(id);
    if (!settled) return;
    if (!outcome.ok) {
      this.#setProposal(id, { ...settled, refusal: outcome.message, state: 'refused' });
      return;
    }
    this.#setProposal(id, { ...settled, state: 'accepted', appliedAt: this.#deps.revision });
  }

  reject(id: number) {
    const plan = this.#planOf(id);
    if (!plan || plan.state === 'accepted') return;
    this.#setProposal(id, { ...plan, state: 'rejected' });
  }

  async replan(id: number) {
    const plan = this.#planOf(id);
    if (!plan || plan.state !== 'ready') return;
    this.#setProposal(id, await this.#vetted(plan, this.#deps.revision));
  }

  #closeReply(usage: Usage | null, error: string | null) {
    this.pending = null;
    const last = this.messages.at(-1);
    if (!last || last.role !== 'assistant') return;
    this.messages = [
      ...this.messages.slice(0, -1),
      { ...last, usage: usage ?? last.usage, error: error ?? last.error }
    ];
  }

  #planOf(id: number): Proposal | null {
    return this.messages.find((message) => message.id === id)?.proposal ?? null;
  }

  #setProposal(id: number, next: Proposal) {
    this.messages = this.messages.map((message) =>
      message.id === id ? { ...message, proposal: next } : message
    );
  }

  async #vetted(plan: Proposal, planned: number): Promise<Proposal> {
    try {
      const checked = await previewCommands(this.#deps.path, plan.commands, planned);
      return {
        ...plan,
        baseRevision: planned,
        diff: diffLines(checked.diff),
        splices: checked.summary.splices,
        refusal: null,
        state: 'ready'
      };
    } catch (error) {
      const record = errorRecord(error);
      if (record?.error === 'references_outside_graph' && typeof record.block === 'string') {
        this.#deps.onRefusal({ block: record.block, spans: spansOf(record) });
      }
      return {
        ...plan,
        baseRevision: planned,
        diff: [],
        splices: 0,
        refusal: describeApplyError(error),
        state: 'refused'
      };
    }
  }

  async #attachProposal(payload: AiAgentProposal) {
    if (!this.#deps.editable || payload.path !== this.#deps.path) return;
    const planned = this.#deps.revision;
    const plan = await this.#vetted(
      {
        rationale: payload.rationale,
        commands: payload.commands,
        baseRevision: planned,
        dropped: payload.dropped,
        diff: [],
        splices: 0,
        refusal: null,
        state: 'ready',
        appliedAt: null
      },
      planned
    );
    const last = this.messages.at(-1);
    if (last && last.role === 'assistant' && last.proposal === null) {
      this.messages = [...this.messages.slice(0, -1), { ...last, proposal: plan }];
      return;
    }
    this.messages = [
      ...this.messages,
      { id: ++this.#turns, role: 'assistant', text: '', usage: null, error: null, proposal: plan }
    ];
  }
}
