const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const cp = require("child_process");

const ARG_STATE_PREFIX = "basl.entryArgs";
const SEMANTIC_TOKEN_TYPES = [
  "namespace",
  "class",
  "enum",
  "interface",
  "function",
  "method",
  "property",
  "parameter",
  "variable",
  "type",
  "keyword",
  "comment",
  "number",
];
const SEMANTIC_TOKEN_MODIFIERS = [
  "declaration",
  "readonly",
  "defaultLibrary",
];

function activate(context) {
  const fallbackCompletions = JSON.parse(
    fs.readFileSync(path.join(__dirname, "completions.json"), "utf8")
  );
  const diagnostics = vscode.languages.createDiagnosticCollection("basl");
  const client = new BaslLSPClient(diagnostics);
  const semanticLegend = new vscode.SemanticTokensLegend(
    SEMANTIC_TOKEN_TYPES,
    SEMANTIC_TOKEN_MODIFIERS
  );
  const baslCommand = () => vscode.workspace.getConfiguration("basl").get("path") || "basl";

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

  function activeBaslDocument() {
    const document = vscode.window.activeTextEditor?.document;
    return isBaslDocument(document) ? document : undefined;
  }

  function documentFromURI(uri) {
    if (uri) {
      return vscode.workspace.textDocuments.find((item) => item.uri.toString() === uri.toString());
    }
    return activeBaslDocument();
  }

  async function entryPointForDocument(document) {
    if (!isBaslDocument(document)) {
      return null;
    }
    if (await ensureClient()) {
      try {
        const symbols = await client.request("textDocument/documentSymbol", {
          textDocument: { uri: document.uri.toString() },
        });
        const entry = findMainSymbol(symbols);
        if (entry?.selectionRange) {
          return { range: toRange(entry.selectionRange) };
        }
        if (entry?.range) {
          return { range: toRange(entry.range) };
        }
      } catch {}
    }
    const fallbackRange = detectMainRange(document);
    return fallbackRange ? { range: fallbackRange } : null;
  }

  async function requireEntryPointDocument(uri) {
    const document = documentFromURI(uri);
    if (!isBaslDocument(document)) {
      vscode.window.showErrorMessage("Open a BASL file with `fn main(...)` to run or debug it.");
      return null;
    }
    if (document.isDirty && !(await document.save())) {
      return null;
    }
    const entry = await entryPointForDocument(document);
    if (!entry) {
      vscode.window.showErrorMessage(`No BASL entry point found in ${path.basename(document.uri.fsPath)}.`);
      return null;
    }
    return { document, entry };
  }

  function argsStateKey(document) {
    return `${ARG_STATE_PREFIX}:${document.uri.fsPath}`;
  }

  function workspaceFolderForDocument(document) {
    return vscode.workspace.getWorkspaceFolder(document.uri);
  }

  function configForDocument(scope, document) {
    return vscode.workspace.getConfiguration(scope, document.uri);
  }

  function defaultArgsForDocument(document) {
    const args = configForDocument("basl", document).get("args");
    return Array.isArray(args) ? args.filter((item) => typeof item === "string") : [];
  }

  function rememberedArgsForDocument(document) {
    const args = context.workspaceState.get(argsStateKey(document));
    return Array.isArray(args) ? args.filter((item) => typeof item === "string") : null;
  }

  function launchConfigsForDocument(document) {
    const folder = workspaceFolderForDocument(document);
    const configs = vscode.workspace.getConfiguration("launch", folder?.uri).get("configurations");
    return Array.isArray(configs) ? configs : [];
  }

  function replaceVar(value, key, replacement) {
    return value.split(key).join(replacement);
  }

  function resolveLaunchValue(value, document, folder) {
    if (typeof value === "string") {
      let out = value;
      const workspacePath = folder?.uri.fsPath || "";
      out = replaceVar(out, "${file}", document.uri.fsPath);
      out = replaceVar(out, "${workspaceFolder}", workspacePath);
      out = replaceVar(out, "${workspaceFolderBasename}", folder?.name || "");
      return out;
    }
    if (Array.isArray(value)) {
      return value
        .map((item) => resolveLaunchValue(item, document, folder))
        .filter((item) => typeof item === "string");
    }
    return value;
  }

  function matchingLaunchConfig(document) {
    const folder = workspaceFolderForDocument(document);
    for (const config of launchConfigsForDocument(document)) {
      if (!config || config.type !== "basl" || (config.request && config.request !== "launch")) {
        continue;
      }
      const resolvedProgram = typeof config.program === "string"
        ? resolveLaunchValue(config.program, document, folder)
        : null;
      if (resolvedProgram === document.uri.fsPath || config.program === "${file}") {
        return {
          args: Array.isArray(config.args) ? resolveLaunchValue(config.args, document, folder) : undefined,
          cwd: typeof config.cwd === "string" ? resolveLaunchValue(config.cwd, document, folder) : undefined,
          path: Array.isArray(config.path) ? resolveLaunchValue(config.path, document, folder) : undefined,
          name: typeof config.name === "string" ? config.name : undefined,
          stopOnEntry: config.stopOnEntry === true,
        };
      }
    }
    return null;
  }

  function runtimeOptionsForDocument(document) {
    const folder = workspaceFolderForDocument(document);
    const launch = matchingLaunchConfig(document);
    const rememberedArgs = rememberedArgsForDocument(document);
    return {
      folder,
      args: rememberedArgs ?? launch?.args ?? defaultArgsForDocument(document),
      cwd: launch?.cwd || folder?.uri.fsPath || path.dirname(document.uri.fsPath),
      searchPaths: Array.isArray(launch?.path) ? launch.path : [],
      stopOnEntry: launch?.stopOnEntry === true,
      name: launch?.name,
    };
  }

  function hasDocumentBreakpoints(document) {
    return vscode.debug.breakpoints.some(
      (item) => item instanceof vscode.SourceBreakpoint && item.location.uri.toString() === document.uri.toString()
    );
  }

  async function promptForArgs(document) {
    const currentArgs = runtimeOptionsForDocument(document).args;
    const input = await vscode.window.showInputBox({
      title: "BASL Program Arguments",
      prompt: "Arguments passed to the BASL program.",
      value: formatArgsForInput(currentArgs),
    });
    if (input === undefined) {
      return undefined;
    }
    const parsed = parseCommandLine(input);
    await context.workspaceState.update(argsStateKey(document), parsed);
    return parsed;
  }

  async function runDocument(uri, promptForCustomArgs = false) {
    const target = await requireEntryPointDocument(uri);
    if (!target) {
      return;
    }
    const { document } = target;
    const options = runtimeOptionsForDocument(document);
    const args = promptForCustomArgs ? await promptForArgs(document) : options.args;
    if (args === undefined) {
      return;
    }
    const terminal = vscode.window.createTerminal({
      name: `BASL: ${path.basename(document.uri.fsPath)}`,
      cwd: options.cwd,
    });
    terminal.show(true);
    terminal.sendText(buildRunCommand(baslCommand(), document.uri.fsPath, options.searchPaths, args));
  }

  async function debugDocument(uri, promptForCustomArgs = false) {
    const target = await requireEntryPointDocument(uri);
    if (!target) {
      return;
    }
    const { document } = target;
    const options = runtimeOptionsForDocument(document);
    const args = promptForCustomArgs ? await promptForArgs(document) : options.args;
    if (args === undefined) {
      return;
    }
    const stopOnEntry = options.stopOnEntry || !hasDocumentBreakpoints(document);
    await vscode.debug.startDebugging(options.folder, {
      type: "basl",
      request: "launch",
      name: options.name || `Debug ${path.basename(document.uri.fsPath)}`,
      program: document.uri.fsPath,
      cwd: options.cwd,
      args,
      path: options.searchPaths,
      stopOnEntry,
      console: "internalConsole",
    });
  }

  context.subscriptions.push(diagnostics);
  context.subscriptions.push({ dispose: () => client.stop() });
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("basl", {
      createDebugAdapterDescriptor() {
        return new vscode.DebugAdapterExecutable(baslCommand(), ["dap"]);
      },
    }),
    vscode.debug.registerDebugConfigurationProvider("basl", {
      async resolveDebugConfiguration(folder, config) {
        if (!config.type) {
          config.type = "basl";
        }
        if (!config.request) {
          config.request = "launch";
        }
        if (!config.name) {
          config.name = "Debug BASL";
        }
        if (!config.program) {
          const active = activeBaslDocument();
          if (active && (await entryPointForDocument(active))) {
            config.program = active.uri.fsPath;
          }
        }
        if (!config.cwd) {
          config.cwd = folder?.uri.fsPath || vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        }
        if (!Array.isArray(config.args)) {
          const active = activeBaslDocument();
          if (active && config.program === active.uri.fsPath) {
            config.args = runtimeOptionsForDocument(active).args;
          }
        }
        if (!Array.isArray(config.path)) {
          const active = activeBaslDocument();
          if (active && config.program === active.uri.fsPath) {
            config.path = runtimeOptionsForDocument(active).searchPaths;
          }
        }
        if (!config.program) {
          vscode.window.showErrorMessage("BASL debugging requires a `program` path.");
          return undefined;
        }
        return config;
      },
    })
  );
  context.subscriptions.push(
    vscode.commands.registerCommand("basl.runEntryPoint", (uri) => runDocument(uri, false)),
    vscode.commands.registerCommand("basl.runEntryPointWithArgs", (uri) => runDocument(uri, true)),
    vscode.commands.registerCommand("basl.debugEntryPoint", (uri) => debugDocument(uri, false)),
    vscode.commands.registerCommand("basl.debugEntryPointWithArgs", (uri) => debugDocument(uri, true))
  );
  context.subscriptions.push(
    vscode.languages.registerCodeLensProvider("basl", {
      async provideCodeLenses(document) {
        if (!isBaslDocument(document)) {
          return [];
        }
        if (configForDocument("basl", document).get("codeLens.entryPoint") === false) {
          return [];
        }
        const entry = await entryPointForDocument(document);
        if (!entry) {
          return [];
        }
        return [
          new vscode.CodeLens(entry.range, {
            title: "$(play) Run",
            command: "basl.runEntryPoint",
            arguments: [document.uri],
          }),
          new vscode.CodeLens(entry.range, {
            title: "$(debug-alt-small) Debug",
            command: "basl.debugEntryPoint",
            arguments: [document.uri],
          }),
          new vscode.CodeLens(entry.range, {
            title: "Args...",
            command: "basl.runEntryPointWithArgs",
            arguments: [document.uri],
          }),
        ];
      },
    })
  );
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
    vscode.languages.registerDeclarationProvider("basl", {
      async provideDeclaration(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/declaration", textDocumentPositionParams(document, position));
        return result ? toLocation(result) : undefined;
      },
    })
  );
  context.subscriptions.push(
    vscode.languages.registerImplementationProvider("basl", {
      async provideImplementation(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/implementation", textDocumentPositionParams(document, position));
        if (!Array.isArray(result)) {
          return result ? toLocation(result) : undefined;
        }
        return result.map(toLocation);
      },
    })
  );
  context.subscriptions.push(
    vscode.languages.registerFoldingRangeProvider("basl", {
      async provideFoldingRanges(document) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/foldingRange", {
          textDocument: { uri: document.uri.toString() },
        });
        if (!Array.isArray(result)) {
          return undefined;
        }
        return result.map((item) => new vscode.FoldingRange(
          item.startLine,
          item.endLine,
          foldingRangeKind(item.kind)
        ));
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
    vscode.languages.registerDocumentHighlightProvider("basl", {
      async provideDocumentHighlights(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/documentHighlight", textDocumentPositionParams(document, position));
        if (!Array.isArray(result)) {
          return undefined;
        }
        return result.map((item) => new vscode.DocumentHighlight(
          toRange(item.range),
          documentHighlightKind(item.kind)
        ));
      },
    })
  );
  context.subscriptions.push(
    vscode.languages.registerDocumentSemanticTokensProvider(
      "basl",
      {
        async provideDocumentSemanticTokens(document) {
          if (!isBaslDocument(document) || !(await ensureClient())) {
            return undefined;
          }
          const result = await client.request("textDocument/semanticTokens/full", {
            textDocument: { uri: document.uri.toString() },
          });
          if (!result || !Array.isArray(result.data)) {
            return undefined;
          }
          return new vscode.SemanticTokens(Uint32Array.from(result.data));
        },
      },
      semanticLegend
    )
  );
  context.subscriptions.push(
    vscode.languages.registerCodeActionsProvider(
      "basl",
      {
        async provideCodeActions(document, range, context) {
          if (!isBaslDocument(document) || !(await ensureClient())) {
            return undefined;
          }
          const result = await client.request("textDocument/codeAction", {
            textDocument: { uri: document.uri.toString() },
            range: toLSPRange(range),
            context: {
              only: Array.isArray(context.only) ? context.only.map((item) => item.value) : undefined,
              diagnostics: (context.diagnostics || []).map((item) => ({
                range: toLSPRange(item.range),
                message: item.message,
                source: item.source || "basl",
              })),
            },
          });
          if (!Array.isArray(result)) {
            return undefined;
          }
          return result.map(toCodeAction);
        },
      },
      {
        providedCodeActionKinds: [
          vscode.CodeActionKind.QuickFix,
          vscode.CodeActionKind.SourceOrganizeImports,
        ],
      }
    )
  );

  context.subscriptions.push(
    vscode.languages.registerRenameProvider("basl", {
      async prepareRename(document, position) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/prepareRename", textDocumentPositionParams(document, position));
        if (!result || !result.range) {
          return undefined;
        }
        return {
          range: toRange(result.range),
          placeholder: result.placeholder,
        };
      },
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
    vscode.languages.registerSignatureHelpProvider(
      "basl",
      {
        async provideSignatureHelp(document, position) {
          if (!isBaslDocument(document) || !(await ensureClient())) {
            return undefined;
          }
          const result = await client.request("textDocument/signatureHelp", textDocumentPositionParams(document, position));
          if (!result || !Array.isArray(result.signatures) || result.signatures.length === 0) {
            return undefined;
          }
          const help = new vscode.SignatureHelp();
          help.activeSignature = result.activeSignature || 0;
          help.activeParameter = result.activeParameter || 0;
          help.signatures = result.signatures.map((item) => {
            const sig = new vscode.SignatureInformation(item.label, markdownFromDocs(item.documentation));
            sig.parameters = Array.isArray(item.parameters)
              ? item.parameters.map((param) => new vscode.ParameterInformation(param.label, markdownFromDocs(param.documentation)))
              : [];
            return sig;
          });
          return help;
        },
      },
      "(",
      ","
    )
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
    vscode.languages.registerWorkspaceSymbolProvider({
      async provideWorkspaceSymbols(query) {
        if (!(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("workspace/symbol", { query });
        if (!Array.isArray(result)) {
          return undefined;
        }
        return result.map((item) => new vscode.SymbolInformation(
          item.name,
          symbolKind(item.kind),
          item.containerName || "",
          toLocation(item.location)
        ));
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
                out.documentation = markdownFromDocs(item.documentation);
                if (typeof item.insertText === "string" && item.insertText.length > 0) {
                  out.insertText = item.insertText;
                }
                if (Array.isArray(item.additionalTextEdits) && item.additionalTextEdits.length > 0) {
                  out.additionalTextEdits = item.additionalTextEdits.map(
                    (edit) => new vscode.TextEdit(toRange(edit.range), edit.newText)
                  );
                }
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

  context.subscriptions.push(
    vscode.languages.registerDocumentFormattingEditProvider("basl", {
      async provideDocumentFormattingEdits(document) {
        if (!isBaslDocument(document) || !(await ensureClient())) {
          return undefined;
        }
        const result = await client.request("textDocument/formatting", {
          textDocument: { uri: document.uri.toString() },
          options: {
            insertSpaces: true,
            tabSize: 4,
          },
        });
        if (!Array.isArray(result)) {
          return undefined;
        }
        return result.map((item) => new vscode.TextEdit(toRange(item.range), item.newText));
      },
    })
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

function toLSPRange(range) {
  return {
    start: toLSPPosition(range.start),
    end: toLSPPosition(range.end),
  };
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

function toCodeAction(item) {
  const action = new vscode.CodeAction(
    item.title,
    item.kind ? new vscode.CodeActionKind(item.kind) : undefined
  );
  action.isPreferred = item.isPreferred === true;
  if (item.edit && item.edit.changes) {
    const edit = new vscode.WorkspaceEdit();
    for (const [uri, edits] of Object.entries(item.edit.changes)) {
      for (const change of edits) {
        edit.replace(vscode.Uri.parse(uri), toRange(change.range), change.newText);
      }
    }
    action.edit = edit;
  }
  return action;
}

function markdownFromDocs(text) {
  if (!text) {
    return undefined;
  }
  const markdown = new vscode.MarkdownString(text);
  markdown.isTrusted = false;
  return markdown;
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

function documentHighlightKind(kind) {
  switch (kind) {
    case 2:
      return vscode.DocumentHighlightKind.Read;
    case 3:
      return vscode.DocumentHighlightKind.Write;
    default:
      return vscode.DocumentHighlightKind.Text;
  }
}

function foldingRangeKind(kind) {
  switch (kind) {
    case "comment":
      return vscode.FoldingRangeKind.Comment;
    case "imports":
      return vscode.FoldingRangeKind.Imports;
    default:
      return undefined;
  }
}

function findMainSymbol(items) {
  if (!Array.isArray(items)) {
    return null;
  }
  for (const item of items) {
    if (item && item.name === "main") {
      return item;
    }
  }
  return null;
}

function detectMainRange(document) {
  const lines = document.getText().split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    if (/^fn\s+main\s*\(/.test(lines[i].trimStart())) {
      const start = lines[i].indexOf("main");
      if (start >= 0) {
        return new vscode.Range(i, start, i, start + "main".length);
      }
    }
  }
  return null;
}

function parseCommandLine(input) {
  const args = [];
  let current = "";
  let quote = null;
  for (let i = 0; i < input.length; i++) {
    const ch = input[i];
    if (quote) {
      if (ch === "\\" && i + 1 < input.length && (input[i + 1] === quote || input[i + 1] === "\\")) {
        current += input[i + 1];
        i++;
        continue;
      }
      if (ch === quote) {
        quote = null;
        continue;
      }
      current += ch;
      continue;
    }
    if (ch === "'" || ch === "\"") {
      quote = ch;
      continue;
    }
    if (/\s/.test(ch)) {
      if (current) {
        args.push(current);
        current = "";
      }
      continue;
    }
    if (ch === "\\" && i + 1 < input.length) {
      current += input[i + 1];
      i++;
      continue;
    }
    current += ch;
  }
  if (current || quote !== null) {
    args.push(current);
  }
  return args;
}

function formatArgsForInput(args) {
  return (Array.isArray(args) ? args : []).map(shellQuote).join(" ");
}

function buildRunCommand(command, program, searchPaths, args) {
  const parts = [command];
  for (const searchPath of searchPaths || []) {
    parts.push("--path", searchPath);
  }
  parts.push(program, ...(args || []));
  return parts.map(shellQuote).join(" ");
}

function shellQuote(value) {
  const text = String(value ?? "");
  if (process.platform === "win32") {
    return `"${text.replace(/"/g, '""')}"`;
  }
  if (text === "") {
    return "''";
  }
  if (/^[A-Za-z0-9_./:=+-]+$/.test(text)) {
    return text;
  }
  return `'${text.replace(/'/g, `'\"'\"'`)}'`;
}

function deactivate() {}

module.exports = { activate, deactivate };
