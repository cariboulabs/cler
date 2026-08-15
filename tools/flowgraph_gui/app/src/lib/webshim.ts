// Tauri IPC bridge over HTTP: talks to e2e_backend (`/invoke`, `/events/poll`).
// Must stay a self-contained function — Playwright serialises it via addInitScript.
export function shim(base: string): void {
  const win = window as unknown as Record<string, unknown>;
  const calls: { cmd: string; args: Record<string, unknown> }[] = [];
  const listeners = new Map<number, { event: string; handler: number }>();
  const callbacks = new Map<number, (payload: unknown) => void>();
  let next = 1;
  let dialogAnswer: unknown = null;

  const post = async (route: string, payload: unknown): Promise<unknown> => {
    const response = await fetch(base + route, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const text = await response.text();
    const body = text.length > 0 ? JSON.parse(text) : {};
    if (response.status !== 200) {
      throw new Error(`e2e backend ${response.status}: ${body.loud ?? text}`);
    }
    if ('err' in body) throw body.err;
    return body.ok;
  };

  const invoke = async (cmd: string, args: Record<string, unknown> = {}): Promise<unknown> => {
    calls.push({ cmd, args });
    if (cmd === 'plugin:dialog|open') return dialogAnswer;
    if (cmd === 'plugin:event|listen') {
      const id = next++;
      listeners.set(id, { event: args.event as string, handler: args.handler as number });
      return id;
    }
    if (cmd === 'plugin:event|unlisten') {
      listeners.delete(args.eventId as number);
      return null;
    }
    return post('/invoke', { cmd, args });
  };

  win.__TAURI_INTERNALS__ = {
    invoke,
    transformCallback(callback: (payload: unknown) => void) {
      const id = next++;
      callbacks.set(id, callback);
      return id;
    },
    metadata: {}
  };
  win.__TAURI_EVENT_PLUGIN_INTERNALS__ = {
    unregisterListener(_event: string, eventId: number) {
      listeners.delete(eventId);
    }
  };
  win.__CLER_E2E__ = {
    calls,
    forget: () => calls.splice(0, calls.length),
    answerDialog: (value: unknown) => {
      dialogAnswer = value;
    },
    emit: (event: string, payload: unknown) => {
      for (const [id, entry] of listeners) {
        if (entry.event !== event) continue;
        callbacks.get(entry.handler)?.({ event, id, payload });
      }
    }
  };

  let ticking = false;
  setInterval(() => {
    if (ticking || listeners.size === 0) return;
    ticking = true;
    void post('/events/poll', {})
      .then((pending) => {
        for (const sent of (pending as { event: string; payload: unknown }[]) ?? []) {
          for (const [id, entry] of listeners) {
            if (entry.event !== sent.event) continue;
            const callback = callbacks.get(entry.handler);
            if (callback) callback({ event: sent.event, id, payload: sent.payload });
          }
        }
      })
      .catch(() => undefined)
      .finally(() => {
        ticking = false;
      });
  }, 200);
}
