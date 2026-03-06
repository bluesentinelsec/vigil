const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const cp = require("child_process");

function activate(context) {
  const fallbackCompletions = JSON.parse(
    fs.readFileSync(path.join(__dirname, "completions.json"), "utf8")
  );
  const diagnostics = vscode.languages.createDiagnosticCollection("basl");
  const client = new BaslLSPClient(diagnostics);

  function isBaslDocument(document) {
    return document && document.languageId === "basl" && document.uri.scheme === "file";
  }

  async function ensureClient() {
    try {
      await client.start();
      return true;
    } catch {
      return false;
    }
  }

  context.subscriptions.push(diagnostics);
  context.subscriptions.push({ dispose: () => client.stop() });
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((document) => {
      if (isBaslDocument(document)) {
        client.didOpen(document);
      }
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (isBaslDocument(event.document)) {
        client.didChange(event.document);
      }
    }),
    vscode.workspace.onDidSaveTextDocument((document) => {
      if (isBaslDocument(document)) {
        client.didSave(document);
      }
    }),
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (isBaslDocument(document)) {
        client.didClose(document);
      }
    })
  );

  context.subscriptions.push(
    vscode.languages.registerDefinitionProvider("basl", {
      async provideDefinition(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/definition", textDocumentPositionParams(document, position));
        return result ? toLocation(result) : undefined;
      },
    })
  );

  context.subscriptions.push(
    vscode.languages.registerHoverProvider("basl", {
      async provideHover(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/hover", textDocumentPositionParams(document, position));
        if (!result || !result.contents || !result.contents.value) {
          return undefined;
        }
        return new vscode.Hover(new vscode.MarkdownString(result.contents.value));
      },
    })
  );

  context.subscriptions.push(
    vscode.languages.registerReferenceProvider("basl", {
      async provideReferences(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/references", textDocumentPositionParams(document, position));
        if (!Array.isArray(result)) {
          return undefined;
        }
        return result.map(toLocation);
      },
    })
  );

  context.subscriptions.push(
    vscode.languages.registerRenameProvider("basl", {
      async provideRenameEdits(document, position, newName) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/rename", {
          textDocument: { uri: document.uri.toString() },
          position: toLSPPosition(position),
          newName,
        });
        if (!result || !result.changes) {
          return undefined;
        }
        const edit = new vscode.WorkspaceEdit();
        for (const [uri, edits] of Object.entries(result.changes)) {
          for (const item of edits) {
            edit.replace(vscode.Uri.parse(uri), toRange(item.range), item.newText);
          }
        }
        return edit;
      },
    })
  );

  context.subscriptions.push(
    vscode.languages.registerDocumentSymbolProvider("basl", {
      async provideDocumentSymbols(document) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/documentSymbol", {
          textDocument: { uri: document.uri.toString() },
        });
        if (!Array.isArray(result)) {
          return undefined;
        }
        return result.map(toDocumentSymbol);
      },
    })
  );

  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      "basl",
      {
        async provideCompletionItems(document, position) {
          if (isBaslDocument(document) && (await ensureClient())) {
            const result = await client.request("textDocument/completion", textDocumentPositionParams(document, position));
            if (Array.isArray(result) && result.length > 0) {
              return result.map((item) => {
                const out = new vscode.CompletionItem(item.label, completionKind(item.kind));
                out.detail = item.detail;
                return out;
              });
            }
          }

          const linePrefix = document.lineAt(position).text.substring(0, position.character);
          const match = linePrefix.match(/(\w+)\.$/);
          if (!match) {
            return undefined;
          }
          const members = fallbackCompletions[match[1]];
          if (!members) {
            return undefined;
          }
          return members.map((name) => {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
            item.detail = `${match[1]}.${name}`;
            return item;
          });
        },
      },
      "."
    )
  );
}

class BaslLSPClient {
  constructor(diagnostics) {
    this.diagnostics = diagnostics;
    this.proc = null;
    this.pending = new Map();
    this.nextId = 1;
    this.buffer = Buffer.alloc(0);
    this.ready = null;
    this.warned = false;
    this.stopped = false;
  }

  baslCommand() {
    const cfg = vscode.workspace.getConfiguration("basl");
    return cfg.get("path") || "basl";
  }

  async start() {
    if (this.ready) {
      return this.ready;
    }
    this.stopped = false;
    this.ready = this.startProcess();
    try {
      await this.ready;
    } catch (error) {
      this.ready = null;
      throw error;
    }
    return this.ready;
  }

  async startProcess() {
    const command = this.baslCommand();
    this.proc = cp.spawn(command, ["lsp"], { stdio: "pipe" });
    this.proc.stdout.on("data", (chunk) => this.handleData(chunk));
    this.proc.stderr.on("data", () => {});
    this.proc.on("exit", () => {
      const pending = this.pending;
      this.pending = new Map();
      for (const { reject } of pending.values()) {
        reject(new Error("BASL language server exited"));
      }
      this.proc = null;
      this.ready = null;
    });
    this.proc.on("error", (error) => {
      if (!this.warned) {
        this.warned = true;
        vscode.window.showWarningMessage(
          "BASL semantic features require the `basl` CLI on your PATH or `basl.path`."
        );
      }
      const pending = this.pending;
      this.pending = new Map();
      for (const { reject } of pending.values()) {
        reject(error);
      }
      this.proc = null;
      this.ready = null;
    });

    const initializeResult = await this.requestInternal("initialize", {
      processId: process.pid,
      rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
      workspaceFolders: (vscode.workspace.workspaceFolders || []).map((folder) => ({
        uri: folder.uri.toString(),
        name: folder.name,
      })),
      capabilities: {},
    });
    this.notifyInternal("initialized", {});
    for (const document of vscode.workspace.textDocuments) {
      if (document.languageId === "basl" && document.uri.scheme === "file") {
        this.notifyInternal("textDocument/didOpen", {
          textDocument: {
            uri: document.uri.toString(),
            version: document.version,
            languageId: "basl",
            text: document.getText(),
          },
        });
      }
    }
    return initializeResult;
  }

  async stop() {
    if (this.stopped) {
      return;
    }
    this.stopped = true;
    if (!this.proc) {
      return;
    }
    try {
      await this.request("shutdown", {});
    } catch {}
    try {
      this.notify("exit", {});
    } catch {}
    this.proc.kill();
    this.proc = null;
    this.ready = null;
  }

  async request(method, params) {
    await this.start();
    return this.requestInternal(method, params);
  }

  requestInternal(method, params) {
    const id = this.nextId++;
    const payload = { jsonrpc: "2.0", id, method, params };
    return new Promise((resolve, reject) => {
      this.pending.set(String(id), { resolve, reject });
      this.write(payload);
    });
  }

  async notify(method, params) {
    await this.start();
    this.notifyInternal(method, params);
  }

  notifyInternal(method, params) {
    this.write({ jsonrpc: "2.0", method, params });
  }

  async didOpen(document) {
    if (!(await this.start().then(() => true).catch(() => false))) {
      return;
    }
    this.notifyInternal("textDocument/didOpen", {
      textDocument: {
        uri: document.uri.toString(),
        version: document.version,
        languageId: "basl",
        text: document.getText(),
      },
    });
  }

  async didChange(document) {
    if (!(await this.start().then(() => true).catch(() => false))) {
      return;
    }
    this.notifyInternal("textDocument/didChange", {
      textDocument: {
        uri: document.uri.toString(),
        version: document.version,
      },
      contentChanges: [{ text: document.getText() }],
    });
  }

  async didSave(document) {
    if (!(await this.start().then(() => true).catch(() => false))) {
      return;
    }
    this.notifyInternal("textDocument/didSave", {
      textDocument: { uri: document.uri.toString() },
    });
  }

  async didClose(document) {
    if (!this.proc) {
      return;
    }
    this.notifyInternal("textDocument/didClose", {
      textDocument: { uri: document.uri.toString() },
    });
    this.diagnostics.delete(document.uri);
  }

  write(payload) {
    if (!this.proc || !this.proc.stdin.writable) {
      throw new Error("BASL language server is not running");
    }
    const body = Buffer.from(JSON.stringify(payload), "utf8");
    const header = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "utf8");
    this.proc.stdin.write(Buffer.concat([header, body]));
  }

  handleData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }
      const header = this.buffer.slice(0, headerEnd).toString("utf8");
      const match = header.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        this.buffer = Buffer.alloc(0);
        return;
      }
      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      if (this.buffer.length < bodyStart + length) {
        return;
      }
      const body = this.buffer.slice(bodyStart, bodyStart + length).toString("utf8");
      this.buffer = this.buffer.slice(bodyStart + length);
      this.handleMessage(JSON.parse(body));
    }
  }

  handleMessage(msg) {
    if (msg.method === "textDocument/publishDiagnostics") {
      this.handleDiagnostics(msg.params);
      return;
    }
    if (msg.id !== undefined && msg.id !== null) {
      const pending = this.pending.get(String(msg.id));
      if (!pending) {
        return;
      }
      this.pending.delete(String(msg.id));
      if (msg.error) {
        pending.reject(new Error(msg.error.message || "BASL language server request failed"));
      } else {
        pending.resolve(msg.result);
      }
    }
  }

  handleDiagnostics(params) {
    if (!params || !params.uri) {
      return;
    }
    const uri = vscode.Uri.parse(params.uri);
    const items = Array.isArray(params.diagnostics) ? params.diagnostics : [];
    this.diagnostics.set(
      uri,
      items.map((item) => {
        const diagnostic = new vscode.Diagnostic(
          toRange(item.range),
          item.message,
          vscode.DiagnosticSeverity.Error
        );
        diagnostic.source = item.source || "basl";
        return diagnostic;
      })
    );
  }
}

function textDocumentPositionParams(document, position) {
  return {
    textDocument: { uri: document.uri.toString() },
    position: toLSPPosition(position),
  };
}

function toLSPPosition(position) {
  return { line: position.line, character: position.character };
}

function toRange(range) {
  return new vscode.Range(
    range.start.line,
    range.start.character,
    range.end.line,
    range.end.character
  );
}

function toLocation(item) {
  return new vscode.Location(vscode.Uri.parse(item.uri), toRange(item.range));
}

function toDocumentSymbol(item) {
  const symbol = new vscode.DocumentSymbol(
    item.name,
    item.detail || "",
    symbolKind(item.kind),
    toRange(item.range),
    toRange(item.selectionRange)
  );
  if (Array.isArray(item.children)) {
    symbol.children = item.children.map(toDocumentSymbol);
  }
  return symbol;
}

function completionKind(kind) {
  switch (kind) {
    case 7:
      return vscode.CompletionItemKind.Class;
    case 2:
      return vscode.CompletionItemKind.Method;
    case 5:
      return vscode.CompletionItemKind.Field;
    case 6:
      return vscode.CompletionItemKind.Variable;
    case 21:
      return vscode.CompletionItemKind.Constant;
    case 9:
      return vscode.CompletionItemKind.Module;
    case 8:
      return vscode.CompletionItemKind.Interface;
    default:
      return vscode.CompletionItemKind.Function;
  }
}

function symbolKind(kind) {
  switch (kind) {
    case 5:
      return vscode.SymbolKind.Class;
    case 6:
      return vscode.SymbolKind.Method;
    case 8:
      return vscode.SymbolKind.Field;
    case 13:
      return vscode.SymbolKind.Variable;
    case 14:
      return vscode.SymbolKind.Constant;
    case 11:
      return vscode.SymbolKind.Interface;
    case 10:
      return vscode.SymbolKind.Enum;
    default:
      return vscode.SymbolKind.Function;
  }
}

function deactivate() {}

module.exports = { activate, deactivate };
