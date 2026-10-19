# Troubleshooting

## The injector cannot find the game

The default target is `RobloxPlayerBeta.exe`. Pass the process name explicitly:

```powershell
.\xeno_injector.exe .\xeno.dll RobloxPlayerBeta.exe
```

Make sure the game is already running. Process lookup is case-insensitive but
matches the executable name exactly.

## `OpenProcess failed` or access denied

Run `xeno_injector.exe` as administrator. The injector needs
`PROCESS_VM_OPERATION`, `PROCESS_VM_WRITE`, `PROCESS_VM_READ`, and
`PROCESS_CREATE_THREAD`.

If the game is protected by an anti-cheat or a process mitigation that blocks
remote threads, manual mapping may not work in that configuration.

## Injection failed

Check the reported error:

- `could not open the DLL` - use the full path to `xeno.dll`.
- `image is not x86-64` - the injector only maps 64-bit images.
- `could not load import library` - a dependency could not be resolved.
- `VirtualAllocEx failed` - the target process has insufficient address space
  or the process handle is not usable.
- `timed out waiting for DllMain` - the entry point did not finish in 20
  seconds. The target may be suspended, or a dependency is blocking.

The injector maps the DLL manually. It does not call `LoadLibrary`, so the DLL
does not appear in the target's module list and cannot be unloaded normally.

## The overlay does not appear

Xeno hooks `IDXGISwapChain::Present`. The overlay can fail to initialize when:

- The game is using exclusive fullscreen. Switch to borderless or windowed.
- The game has not created a D3D11 swapchain yet. Wait until a game is loaded.
- The render hooks were not installed. Check the Console for
  `render hooks installed`.
- `SetWindowLongPtrW` failed for the game window. This is logged as
  `could not subclass the game window`; input handling may be limited even if
  drawing works.

Press `INSERT` to toggle the overlay. The overlay starts visible after a
successful attach.

## `waiting for state`

The signature hook may be installed, but no matching Luau state has resumed
yet. Join a game so Roblox creates a Luau state with a `game` global. The state
check requires `game` to be a userdata.

The status also remains `waiting for state` if the signature matched the wrong
function. Verify the hook address and the signature.

## `lua_resume signature not found`

The client updated or the default pattern no longer matches. Follow
[`UPDATING.md`](UPDATING.md) to derive a new pattern and place it in
`%LOCALAPPDATA%\Xeno\signatures.ini`.

## Scripts do not run

- Scripts are queued and executed on the next `lua_resume` call. A script
  queued while the state is idle waits for the next resume.
- A script with a yield (`task.wait`, `wait`, `coroutine.yield`) fails with
  `script yielded; yielding is not supported by the executor runner`.
- Compilation and runtime errors appear in the Console tab. The runner also
  logs `script failed: ...`.
- The Editor's Execute button is disabled until a Luau state is attached.

## Autoexec scripts do not run

Place `.lua` files directly in:

```text
%LOCALAPPDATA%\Xeno\autoexec
```

They are queued when Xeno first attaches to a Luau state. Empty files are
skipped. Check the Console for `autoexec queued: <name>`.

`queue_on_teleport` writes a timestamped file into the same folder; it does not
subscribe to teleport events.

## HTTP requests fail

`request` uses WinHTTP and is synchronous:

- Include the scheme in `Url` (`https://...`, not `example.com`).
- `Body` is sent as a raw string. Encode JSON yourself.
- TLS errors are reported as a WinHTTP failure with a numeric error code.
- The game thread blocks until the request finishes or the timeout expires.

## The build fails

- Use the Visual Studio 2022 x64 generator or the `msvc-x64` preset.
- CMake must be able to clone Luau, MinHook, and Dear ImGui. If your network
  blocks Git, populate `build/_deps` manually or use a local mirror.
- Do not mix runtime libraries. The default is the static MSVC runtime
  (`XENO_USE_STATIC_RUNTIME=ON`).
- Luau headers use C++ linkage by default. Do not wrap them in `extern "C"`.
- Xeno builds Luau with `LUA_USE_LONGJMP=1`. Keep that definition when
  updating Luau; it avoids C++ unwinding through the manually mapped image.
- Obsolete ImGui enum aliases are disabled for Xeno targets. Use current
  names such as `ImGuiCol_TabSelected` and `ImGuiCol_NavCursor`.

## The DLL crashes after injection

The manual mapper does not run TLS callbacks, process delay-load imports, or
register x64 exception tables. Xeno avoids `thread_local` and uses the static
CRT, but third-party code with TLS or structured exception handling can still
be unsafe in a manually mapped image.

Collect the failure point from a debugger attached to the game process. If the
crash happens before the Console shows `booting`, the issue is in image
loading, imports, or CRT initialization.
