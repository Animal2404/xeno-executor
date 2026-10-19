#pragma once

// This is a starting pattern, not a universal signature. Roblox rebuilds the
// Luau VM every release, so these bytes rotate. Verify it against the client
// build you are attaching to, then override it at runtime through
// %LOCALAPPDATA%\Xeno\signatures.ini:
//
//   lua_resume = 48 89 5C 24 ?? ...
//
// See docs/UPDATING.md for the derivation workflow. Patterns support ?? and ?
// wildcards. The scanner only walks executable PE sections.

namespace xeno::signatures {

inline constexpr char lua_resume[] =
    "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 30 49 8B D8 48 8B F2 48 8B F9 "
    "48 85 D2 74 ?? 48 8B 42 ?? 48 8B 48 ?? 48 89 4F ??";

}
