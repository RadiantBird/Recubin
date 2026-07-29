# Recubin Luau vendor

This directory is based on the official Luau `0.731` tag:

- Commit: `f8ca77acdcb50241e3da21af663f8ef97b4b5ce4`
- Source: <https://github.com/luau-lang/luau/releases/tag/0.731>

Recubin carries one VM extension over that upstream snapshot:

- `table.deepcopy` in `VM/src/ltablib.cpp`, its builtin type declaration in
  `Analysis/src/BuiltinDefinitions.cpp`, and conformance coverage in
  `tests/conformance/tables.luau`.
- Runtime conformance coverage for Luau `const` syntax in
  `tests/conformance/const.luau`, registered by `tests/Conformance.test.cpp`.

The vendored source intentionally does not contain nested Git metadata.
