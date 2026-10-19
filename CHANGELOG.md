# Changelog

All notable changes to Xeno are documented here.

## 0.1.0 - 2026-10-19

Initial repository release.

### Added

- Windows x64 manual-map injector with PE validation, relocation handling,
  import resolution, section permissions, and an entry-point stub.
- `xeno.dll` runtime with a MinHook `lua_resume` detour.
- Script queue that compiles and executes Luau on the game state.
- Executor API: `getgenv`, `getrenv`, `identifyexecutor`, `getexecutorname`,
  `checkcaller`, `iscclosure`, `islclosure`, `newcclosure`, `loadstring`,
  `hookmetamethod`, `getrawmetatable`, `getnamecallmethod`, `setclipboard`,
  `queue_on_teleport`, `request`, `http_request`, and the `syn` aliases.
- D3D11/Dear ImGui overlay with Editor, Console, Settings, and About tabs.
- Runtime signature overrides through
  `%LOCALAPPDATA%\Xeno\signatures.ini`.
- Autoexec folder at `%LOCALAPPDATA%\Xeno\autoexec`.
- CMake presets, GitHub Actions build workflow, API documentation, and
  example script.
- Public download folder:
  https://www.mediafire.com/folder/7q4dz0de1v2y0/xeno+executor
- Promotional README layout with badges, quick start, and UI previews.
- Preview assets: `assets/ui-editor.svg`, `assets/ui-console.svg`,
  `assets/download-button.svg`, and `assets/social-preview.png`.
