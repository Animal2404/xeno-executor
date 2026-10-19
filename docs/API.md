# Xeno API

Xeno installs its API into the game's global environment the first time it sees
a Luau state that exposes `game`. The functions below are available to every
script queued after that point.

Scripts execute on a fresh Luau thread from the `lua_resume` hook. They run
synchronously: yielding from the runner is not supported. Xeno does not set a
Roblox thread identity on that thread, so identity-gated APIs may reject calls.

## Environment

### `getgenv()`

Returns the executor-owned environment table. The table is stored in the Lua
registry and is shared by scripts running on the same Luau state.

```lua
local env = getgenv()
env.loaded = true
print(getgenv().loaded) -- true
```

### `getrenv()`

Returns the game's global environment table.

```lua
local gameenv = getrenv()
print(gameenv.game)
```

### `identifyexecutor()`

Returns two values: the executor name and version.

```lua
local name, version = identifyexecutor()
print(name, version) -- Xeno 0.1.0
```

### `getexecutorname()`

Returns the executor name as a single value.

```lua
print(getexecutorname()) -- Xeno
```

### `checkcaller()`

Returns `true` while a Xeno script is executing. It is an execution-state
approximation, not a full scheduler-context check.

```lua
print(checkcaller()) -- true inside a queued script
```

## Closures

### `iscclosure(function)`

Returns `true` when the value is a C closure.

### `islclosure(function)`

Returns `true` when the value is a Luau closure.

### `newcclosure(function)`

Wraps a Luau function in a C closure. The returned function forwards arguments
and returns all results.

```lua
local wrapped = newcclosure(function(a, b)
    return a + b
end)

print(iscclosure(wrapped)) -- true
print(wrapped(2, 3))       -- 5
```

The proxy uses a protected call. A wrapped function cannot yield across the
proxy boundary.

### `loadstring(source, chunkname?)`

Compiles Luau source and returns the chunk function. On failure it returns
`nil, error`.

```lua
local chunk, err = loadstring("return 21 * 2", "example")
if chunk then
    print(chunk()) -- 42
else
    warn(err)
end
```

### `hookmetamethod(object, method, hook)`

Replaces `method` in the object's metatable and returns the previous function.

```lua
local old_index
local proxy = setmetatable({}, {
    __index = function(self, key)
        return old_index(self, key)
    end,
})

old_index = hookmetamethod(proxy, "__index", function(self, key)
    if key == "secret" then
        return "intercepted"
    end
    return old_index(self, key)
end)

print(proxy.secret) -- intercepted
```

### `getrawmetatable(object)`

Returns the raw metatable, ignoring `__metatable`. Returns `nil` when the
object has no metatable.

### `getnamecallmethod()`

Returns the current namecall method when called from a method invocation. It
returns `nil` outside a namecall.

## Clipboard

### `setclipboard(text)`

Copies UTF-8 text to the Windows clipboard. Returns `true` on success.

```lua
print(setclipboard("hello from Xeno"))
```

## Script queue

### `queue_on_teleport(source)`

Writes the source into `%LOCALAPPDATA%\Xeno\autoexec` with a timestamped file
name and returns `true` when the file was written.

In this implementation the file is executed the next time Xeno attaches to a
Luau state. It is not connected to Roblox `TeleportService` events.

```lua
queue_on_teleport([[
    print("queued by Xeno")
]])
```

## HTTP

### `request(options)`

### `syn.request(options)`

### `syn.http_request(options)`

Performs a synchronous HTTP request through WinHTTP and returns a response
table. If WinHTTP cannot complete the request, it returns `nil, error`.

Options:

| Field | Type | Notes |
| --- | --- | --- |
| `Url` | string | Required. `http` or `https`. |
| `Method` | string | Optional. Defaults to `GET`. |
| `Headers` | table | Optional. String keys and values. |
| `Body` | string | Optional. Sent as-is; JSON is not encoded automatically. |

Response:

| Field | Type | Notes |
| --- | --- | --- |
| `Success` | boolean | The request completed at the WinHTTP layer. |
| `Ok` | boolean | Status code is in the 200-299 range. |
| `StatusCode` | number | HTTP status code. |
| `StatusMessage` | string | HTTP reason phrase. |
| `Body` | string | Raw response body. |
| `Headers` | table | Parsed response headers. |
| `RawHeaders` | string | Raw response headers as returned by WinHTTP. |

```lua
local response = request({
    Url = "https://httpbin.org/get",
    Method = "GET",
})

if response.Ok then
    print(response.StatusCode)
    print(response.Body)
else
    warn(response.StatusMessage)
end
```

The call blocks the game thread until the request finishes or WinHTTP reaches
its timeout. Use it for short requests only.

## Notes

- The API is installed per Luau state. Rejoining a game creates a new state and
  reinstalls the functions.
- `getgenv()` data does not automatically carry across Luau states. Persist
  values in the autoexec folder if they must survive a rejoin.
- Errors raised by queued scripts are written to the Console tab. They are not
  propagated to the game.
