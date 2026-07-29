import {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  InitializeParams,
  TextDocumentSyncKind,
  InitializeResult,
  Diagnostic,
  DiagnosticSeverity,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

import { Parser } from "../../luar/src/parser/parser";
import { Checker } from "../../luar/src/checker/checker";

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

connection.onInitialize((_params: InitializeParams): InitializeResult => ({
  capabilities: {
    textDocumentSync: TextDocumentSyncKind.Incremental,
  },
}));

documents.onDidChangeContent(({ document }) => validate(document));
documents.onDidOpen(({ document }) => validate(document));
documents.onDidClose(({ document }) => {
  connection.sendDiagnostics({ uri: document.uri, diagnostics: [] });
});

function validate(document: TextDocument): void {
  const src = document.getText();
  const diagnostics: Diagnostic[] = [];

  try {
    const prog = new Parser(src).parse();
    const errors = new Checker().check(prog);

    for (const e of errors) {
      diagnostics.push({
        severity: DiagnosticSeverity.Error,
        range: {
          start: { line: e.line - 1, character: e.col - 1 },
          end:   { line: e.line - 1, character: e.col - 1 + 1 },
        },
        message: e.message,
        source: "luar",
      });
    }
  } catch (e: unknown) {
    if (e instanceof Error) {
      const located = e as Error & { line?: number; col?: number };
      const line = typeof located.line === "number" ? located.line - 1 : 0;
      const col  = typeof located.col  === "number" ? located.col  - 1 : 0;
      diagnostics.push({
        severity: DiagnosticSeverity.Error,
        range: {
          start: { line, character: col },
          end:   { line, character: col + 1 },
        },
        message: e.message,
        source: "luar",
      });
    }
  }

  connection.sendDiagnostics({ uri: document.uri, diagnostics });
}

documents.listen(connection);
connection.listen();
