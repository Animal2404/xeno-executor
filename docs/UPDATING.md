# Updating Xeno

Xeno depends on three things that can change independently:

1. The Roblox client build.
2. The Luau version used for compiling and loading scripts.
3. MinHook and Dear ImGui.

## Roblox updates

Roblox rebuilds `RobloxPlayerBeta.exe` frequently. The `lua_resume` function
moves and its machine code changes. The default pattern in
`src/dll/signatures.hpp` is a development placeholder; treat it as a starting
point, not a guarantee.

When the Console reports:

```text
lua_resume signature not found
```

the client has moved or changed the function.

### 1. Find `lua_resume`

`lua_resume` has the signature:

```c
int lua_resume(lua_State* L, lua_State* from, int nargs);
```

On x64, the first three arguments arrive in `RCX`, `RDX`, and `R8`. Useful
anchors include the function prologue, stack checks, and calls into the Luau
VM. Work against the exact client build you are targeting.

### 2. Build a pattern

The scanner supports bytes and wildcards:

- `48 89 5C 24 ??` - fixed bytes with a one-byte wildcard.
- `48 89 5C 24 ?` - the same pattern using a single-character wildcard.
- Spaces are optional between bytes.

Choose a signature that:

- Starts at or before the function entry point.
- Is unique inside executable sections of the main module.
- Avoids relative addresses and immediates that move with code layout.
- Is long enough to avoid matching unrelated prologues.

The scanner walks executable PE sections of the main module and returns the
first match. It does not scan loaded DLLs.

### 3. Override at runtime

Create:

```text
%LOCALAPPDATA%\Xeno\signatures.ini
```

and add:

```ini
lua_resume = 48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 30 49 8B D8 48 8B F2
```

Lines beginning with `#` or `;` are ignored. Whitespace around the key and
value is trimmed. The file is read when `state::install()` runs, so restart the
injector after editing it.

### 4. Verify

Launch the game, inject, and join a game. The Console should show:

```text
lua_resume hooked at 0x...
Xeno attached to Luau state 0x...
```

If the hook succeeds but scripts do not run, the signature may have matched the
wrong function. Revert to a stricter pattern and check the call site.

## Luau updates

Luau is pinned in the top-level `CMakeLists.txt`:

```cmake
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG 0.635
)
```

Before changing the tag:

1. Check the VM and compiler API for renamed or changed functions.
2. Verify `lua_CompileOptions` fields.
3. Verify `lua_pushcclosure` and `lua_pushcclosurek` signatures.
4. Verify `lua_resume` and `luau_load` signatures.
5. Rebuild both the DLL and the injector.

Xeno uses the default C++ linkage of the Luau libraries. Do not wrap Luau
headers in `extern "C"` unless you also build Luau with `LUAU_EXTERN_C=ON`.

The top-level CMake file adds `LUA_USE_LONGJMP=1` to `Luau.VM`. This keeps
Luau error recovery inside the VM and avoids C++ stack unwinding through a
manually mapped image that has no registered x64 unwind tables. Keep that
definition when updating Luau.

## MinHook and Dear ImGui

Both dependencies are pinned in `CMakeLists.txt`. When updating:

- MinHook: verify the CMake target is still named `minhook` and includes the
  `include` directory.
- Dear ImGui: verify the backend file names and enum names used by
  `overlay.cpp`. Obsolete enum aliases are disabled for the Xeno targets.

## Release checklist

- Configure and build with the Visual Studio 2022 x64 generator.
- Confirm `xeno.dll` and `xeno_injector.exe` are produced in the Release
  output directories.
- Test injection on a clean game launch.
- Confirm the overlay appears and toggles with `INSERT`.
- Run a script from `scripts/example.lua`.
- Check the Console for signature and attach errors.
- Upload the DLL and injector to the download folder.
