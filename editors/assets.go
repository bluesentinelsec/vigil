package editors

import "embed"

// Assets contains the bundled editor integration files shipped with basl.
//
//go:embed vim/ftdetect/basl.vim vim/syntax/basl.vim vscode/package.json vscode/extension.js vscode/completions.json vscode/language-configuration.json vscode/snippets/basl.json vscode/syntaxes/basl.tmLanguage.json
var Assets embed.FS
