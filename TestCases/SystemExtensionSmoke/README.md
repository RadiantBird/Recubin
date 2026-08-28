# SystemExtensionSmoke

This is a manual smoke fixture for the System I/O, IPC, and TextFile extensions.
It uses the fixed ApplicationId `9b4d2c11-5e73-4a6f-8c20-1d9f7b3e6a42`, enables
I/O and IPC, and leaves external file access disabled.

## Editor Play

1. Open `SystemExtensionSmoke.yaml` in the editor.
2. Press Play.
3. Inspect the `SmokeResultsLeft`/`SmokeResultsRight` labels and the output console. Every operation is
   reported as `PASS` or `FAIL`; the script continues after expected errors.
4. Stop Play, then Play again. The TextFile line should show the prior overlay
   content for that editor project root.

## Packaged runtime

From the repository root, the dedicated test helper can package the fixture:

```text
build/Release/RecubinTest.exe --package-system-extension-smoke <output-dir>
```

The helper uses the repository root as the project root and creates
`SystemExtensionSmokePackage`. Launch the generated package executable and inspect the
same label and console output. Runtime storage is separate from Editor storage because it uses the packaged content root,
so its first Content value should be the seed (or an earlier runtime overlay),
not the Editor overlay.

The existing Packager UI can also be used to package
`TestCases/SystemExtensionSmoke/SystemExtensionSmoke.yaml` with the repository
root as the project root. Launch the generated package and inspect the

The fixture intentionally performs only relative paths under a temporary
`SystemExtensionSmoke` directory. It verifies text and binary NUL I/O, append,
type queries, sorted listing fields, copy/move/remove, overwrite policy,
traversal and absolute-path rejection, recursive cleanup, IPC stub errors, and
TextFile privacy/clone/creation restrictions.
