const vscode = require("vscode");
const path = require("path");
const fs = require("fs");

function activate(context) {
  const completions = JSON.parse(
    fs.readFileSync(path.join(__dirname, "completions.json"), "utf8")
  );

  const provider = vscode.languages.registerCompletionItemProvider(
    "basl",
    {
      provideCompletionItems(document, position) {
        const linePrefix = document
          .lineAt(position)
          .text.substring(0, position.character);
        const match = linePrefix.match(/(\w+)\.$/);
        if (!match) return undefined;

        const mod = match[1];
        const members = completions[mod];
        if (!members) return undefined;

        return members.map((name) => {
          const item = new vscode.CompletionItem(
            name,
            vscode.CompletionItemKind.Function
          );
          item.detail = `${mod}.${name}`;
          return item;
        });
      },
    },
    "."
  );

  context.subscriptions.push(provider);
}

function deactivate() {}

module.exports = { activate, deactivate };
