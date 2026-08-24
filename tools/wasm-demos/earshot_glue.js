// Stands in for the network between earshot's client and server when both live
// in one page: the client's `new WebSocket(url)` gets this bridge instead, and
// the wasm build of the server (web_server_wasm.cpp) delivers frames through
// window.__earshotFrame. Loaded before earshot_core.js and app.js.
(() => {
  let sock = null;
  let up = false;

  window.Module = {
    arguments: ['--source', 'sim'],
    print: (t) => console.log(t),
    printErr: (t) => console.error(t),
  };

  class EarshotSocket {
    constructor() {
      this.readyState = 0;
      this.binaryType = 'arraybuffer';
      this.onopen = this.onclose = this.onerror = this.onmessage = null;
      sock = this;
      if (up) queueMicrotask(() => this._open());
    }
    _open() {
      if (this.readyState !== 0) return;
      this.readyState = 1;
      if (this.onopen) this.onopen();
      Module._earshot_ws_open();
    }
    send(text) {
      if (this.readyState !== 1) return;
      const n = Module.lengthBytesUTF8(text) + 1;
      const p = Module._malloc(n);
      Module.stringToUTF8(text, p, n);
      Module._earshot_ws_send(p);
      Module._free(p);
    }
    close() {}
  }
  window.WebSocket = EarshotSocket;

  window.__earshotFrame = (ptr, len, isText) => {
    const bytes = Module.HEAPU8.slice(ptr, ptr + len);
    Module._free(ptr);
    if (!sock || sock.readyState !== 1 || !sock.onmessage) return;
    sock.onmessage({ data: isText ? new TextDecoder().decode(bytes) : bytes.buffer });
  };

  window.__earshotReady = () => {
    up = true;
    if (sock) sock._open();
  };
})();
