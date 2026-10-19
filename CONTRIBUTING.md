# Contributing

## Build locally

```powershell
cmake --preset msvc-x64
cmake --build --preset release
```

The first configure downloads Luau, MinHook, and Dear ImGui. The Visual Studio
preset is multi-config, so Debug uses the same build tree:

```powershell
cmake --build --preset debug
```

## Before opening a pull request

- Build both `xeno.dll` and `xeno_injector.exe`.
- Test injection against a clean game launch.
- Confirm the overlay opens with `INSERT` and a script from
  `scripts/example.lua` runs without a compiler or runtime error.
- Update `docs/API.md` if the public Lua surface changes.
- Update `docs/UPDATING.md` if dependency or signature behavior changes.
- Keep changes focused; avoid unrelated formatting churn.

## Code conventions

- C++20, four-space indentation, braces on the same line.
- Keep implementation files under `src/dll` and `src/injector`.
- Use `xeno::` namespaces and the existing `snake_case` naming.
- Do not add `thread_local` storage to the DLL; manually mapped images do not
  receive loader TLS initialization.
- Do not wrap Luau headers in `extern "C"` unless the Luau CMake target is
  built with `LUAU_EXTERN_C=ON`.
- Prefer `std::atomic` for state shared with render or hook threads.
- Do not commit build outputs, local signature files, logs, or autoexec files.

## Signatures

Do not claim a default signature is current without testing it against a known
client build. If you calibrate a pattern, include the client version and the
method used in the pull request description.

## Commits

Use short imperative subjects, for example:

```text
fix: handle imports without OriginalFirstThunk
docs: document synchronous request behavior
```
