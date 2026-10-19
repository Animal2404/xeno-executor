#include "api.hpp"

#include "execution.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <lua.h>
#include <luacode.h>

namespace xeno::api {
namespace {

constexpr int registry_marker = 0x58454E4F;
constexpr int registry_genv = 0x58454E50;

int raise_error(lua_State* state, const char* message) {
    lua_pushstring(state, message);
    return lua_error(state);
}

int request_fail(lua_State* state, const std::string& message) {
    lua_settop(state, 0);
    lua_pushnil(state);
    lua_pushlstring(state, message.data(), message.size());
    return 2;
}

std::string field_string(lua_State* state, int table, const char* key) {
    lua_getfield(state, table, key);
    size_t length = 0;
    const char* value = lua_tolstring(state, -1, &length);
    std::string result = value ? std::string(value, length) : std::string{};
    lua_settop(state, -2);
    return result;
}

int api_getgenv(lua_State* state) {
    lua_rawgeti(state, LUA_REGISTRYINDEX, registry_genv);
    if (lua_type(state, -1) != LUA_TTABLE) {
        lua_settop(state, -2);
        lua_newtable(state);
        lua_pushvalue(state, -1);
        lua_rawseti(state, LUA_REGISTRYINDEX, registry_genv);
    }
    return 1;
}

int api_getrenv(lua_State* state) {
    lua_pushvalue(state, LUA_GLOBALSINDEX);
    return 1;
}

int api_identify(lua_State* state) {
    lua_pushstring(state, XENO_NAME);
    lua_pushstring(state, XENO_VERSION);
    return 2;
}

int api_executor_name(lua_State* state) {
    lua_pushstring(state, XENO_NAME);
    return 1;
}

int api_checkcaller(lua_State* state) {
    lua_pushboolean(state, execution::is_executing() ? 1 : 0);
    return 1;
}

int api_iscclosure(lua_State* state) {
    lua_pushboolean(state, lua_iscfunction(state, 1) ? 1 : 0);
    return 1;
}

int api_islclosure(lua_State* state) {
    const bool is_lua =
        lua_type(state, 1) == LUA_TFUNCTION && lua_iscfunction(state, 1) == 0;
    lua_pushboolean(state, is_lua ? 1 : 0);
    return 1;
}

int proxy_call(lua_State* state) {
    const int arguments = lua_gettop(state);
    if (!lua_checkstack(state, arguments + 4)) {
        return raise_error(state, "newcclosure: stack exhausted");
    }

    lua_pushvalue(state, lua_upvalueindex(1));
    if (arguments > 0) {
        lua_insert(state, 1);
    }

    const int status = lua_pcall(state, arguments, LUA_MULTRET, 0);
    if (status != 0) {
        return lua_error(state);
    }
    return lua_gettop(state);
}

int api_newcclosure(lua_State* state) {
    if (lua_type(state, 1) != LUA_TFUNCTION) {
        return raise_error(state, "newcclosure(function) expected");
    }
    lua_pushvalue(state, 1);
    lua_pushcclosure(state, proxy_call, nullptr, 1);
    return 1;
}

int api_loadstring(lua_State* state) {
    size_t length = 0;
    const char* source = lua_tolstring(state, 1, &length);
    if (!source) {
        return raise_error(state, "loadstring(source) expected");
    }

    const char* raw_name = lua_tolstring(state, 2, nullptr);
    const std::string chunk_name =
        raw_name ? "=" + std::string(raw_name) : "=xeno_loadstring";

    lua_CompileOptions options{};
    options.optimizationLevel = 1;
    options.debugLevel = 1;

    size_t bytecode_size = 0;
    char* bytecode = luau_compile(source, length, &options, &bytecode_size);
    if (!bytecode) {
        lua_settop(state, 0);
        lua_pushnil(state);
        lua_pushstring(state, "compiler failed");
        return 2;
    }

    const int loaded =
        luau_load(state, chunk_name.c_str(), bytecode, bytecode_size, 0);
    std::free(bytecode);

    if (loaded != 0) {
        size_t message_length = 0;
        const char* message = lua_tolstring(state, -1, &message_length);
        std::string text = message ? std::string(message, message_length) : "compile error";
        lua_settop(state, 0);
        lua_pushnil(state);
        lua_pushlstring(state, text.data(), text.size());
        return 2;
    }

    return 1;
}

int api_hookmetamethod(lua_State* state) {
    if (lua_type(state, 1) == LUA_TNIL ||
        lua_type(state, 2) != LUA_TSTRING ||
        lua_type(state, 3) != LUA_TFUNCTION) {
        return raise_error(state, "hookmetamethod(object, method, hook) expected");
    }
    if (!lua_getmetatable(state, 1)) {
        return raise_error(state, "object has no metatable");
    }

    const int metatable = lua_gettop(state);
    lua_pushvalue(state, 2);
    lua_rawget(state, metatable);

    if (lua_type(state, -1) != LUA_TFUNCTION) {
        return raise_error(state, "metamethod is not a function");
    }

    lua_pushvalue(state, 2);
    lua_pushvalue(state, 3);
    lua_rawset(state, metatable);

    return 1;
}

int api_getrawmetatable(lua_State* state) {
    if (!lua_getmetatable(state, 1)) {
        lua_pushnil(state);
    }
    return 1;
}

int api_getnamecallmethod(lua_State* state) {
    const char* method = lua_namecallatom(state, nullptr);
    if (method) {
        lua_pushstring(state, method);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

int api_setclipboard(lua_State* state) {
    size_t length = 0;
    const char* text = lua_tolstring(state, 1, &length);
    bool copied = false;

    if (text && OpenClipboard(nullptr)) {
        EmptyClipboard();

        const int wide_length =
            MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0);
        if (wide_length > 0) {
            HGLOBAL memory = GlobalAlloc(
                GMEM_MOVEABLE, (static_cast<size_t>(wide_length) + 1) * sizeof(wchar_t));
            if (memory) {
                auto* buffer = static_cast<wchar_t*>(GlobalLock(memory));
                if (buffer) {
                    MultiByteToWideChar(
                        CP_UTF8, 0, text, static_cast<int>(length), buffer, wide_length);
                    buffer[wide_length] = L'\0';
                    GlobalUnlock(memory);
                    copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
                    if (!copied) {
                        GlobalFree(memory);
                    }
                } else {
                    GlobalFree(memory);
                }
            }
        }

        CloseClipboard();
    }

    lua_pushboolean(state, copied ? 1 : 0);
    return 1;
}

int api_queue_on_teleport(lua_State* state) {
    size_t length = 0;
    const char* source = lua_tolstring(state, 1, &length);
    if (!source) {
        return raise_error(state, "queue_on_teleport(source) expected");
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);

    char name[80]{};
    std::snprintf(
        name,
        sizeof(name),
        "queued_%04u%02u%02u_%02u%02u%02u.lua",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);

    const auto path = util::autoexec_dir() / name;
    const bool written = util::write_text(path, std::string_view(source, length));

    lua_pushboolean(state, written ? 1 : 0);
    return 1;
}

struct winhttp_guard {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~winhttp_guard() {
        if (request) {
            WinHttpCloseHandle(request);
        }
        if (connection) {
            WinHttpCloseHandle(connection);
        }
        if (session) {
            WinHttpCloseHandle(session);
        }
    }
};

bool query_wide_string(HINTERNET request, DWORD query, std::wstring& output) {
    DWORD size = 0;
    WinHttpQueryHeaders(
        request,
        query,
        WINHTTP_HEADER_NAME_BY_INDEX,
        nullptr,
        &size,
        WINHTTP_NO_HEADER_INDEX);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return false;
    }

    output.resize(size / sizeof(wchar_t));
    if (!WinHttpQueryHeaders(
            request,
            query,
            WINHTTP_HEADER_NAME_BY_INDEX,
            output.data(),
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        output.clear();
        return false;
    }

    while (!output.empty() && output.back() == L'\0') {
        output.pop_back();
    }
    return true;
}

void push_header_table(lua_State* state, const std::string& raw) {
    lua_newtable(state);

    size_t cursor = 0;
    bool status_line = true;
    while (cursor < raw.size()) {
        const size_t end = raw.find("\r\n", cursor);
        const std::string line = raw.substr(
            cursor, end == std::string::npos ? std::string::npos : end - cursor);

        if (!status_line && !line.empty()) {
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                const std::string key = util::trim(line.substr(0, colon));
                const std::string value = util::trim(line.substr(colon + 1));
                if (!key.empty()) {
                    lua_pushlstring(state, key.data(), key.size());
                    lua_pushlstring(state, value.data(), value.size());
                    lua_rawset(state, -3);
                }
            }
        }

        status_line = false;
        if (end == std::string::npos) {
            break;
        }
        cursor = end + 2;
    }
}

std::wstring header_block(lua_State* state) {
    std::wstring result;
    if (lua_type(state, -1) != LUA_TTABLE) {
        lua_settop(state, -2);
        return result;
    }

    const int table = lua_gettop(state);
    lua_pushnil(state);
    while (lua_next(state, table)) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            const char* key = lua_tolstring(state, -2, nullptr);
            const char* value = lua_tolstring(state, -1, nullptr);
            if (key && value) {
                result += util::widen(key);
                result += L": ";
                result += util::widen(value);
                result += L"\r\n";
            }
        }
        lua_settop(state, -2);
    }

    lua_settop(state, -2);
    return result;
}

int api_request(lua_State* state) {
    if (lua_type(state, 1) != LUA_TTABLE) {
        return request_fail(state, "request(options): table expected");
    }

    const std::string url = field_string(state, 1, "Url");
    std::string method = field_string(state, 1, "Method");
    const std::string body = field_string(state, 1, "Body");

    lua_getfield(state, 1, "Headers");
    const std::wstring headers = header_block(state);

    if (url.empty()) {
        return request_fail(state, "request(options): Url is required");
    }
    if (method.empty()) {
        method = "GET";
    }

    lua_settop(state, 0);

    const std::wstring wide_url = util::widen(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(
            wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &components)) {
        return request_fail(state, "request(options): invalid Url");
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    INTERNET_PORT port = components.nPort;
    if (port == 0) {
        port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }

    winhttp_guard client;
    const std::wstring user_agent = std::wstring(L"Xeno/") + util::widen(XENO_VERSION);

    client.session = WinHttpOpen(
        user_agent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!client.session) {
        return request_fail(
            state, "WinHTTP could not open a session (" +
                       std::to_string(GetLastError()) + ")");
    }

    WinHttpSetTimeouts(client.session, 10000, 10000, 15000, 20000);

    client.connection = WinHttpConnect(client.session, host.c_str(), port, 0);
    if (!client.connection) {
        return request_fail(
            state, "WinHTTP could not connect (" + std::to_string(GetLastError()) +
                       ")");
    }

    const std::wstring wide_method = util::widen(method);
    client.request = WinHttpOpenRequest(
        client.connection,
        wide_method.c_str(),
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!client.request) {
        return request_fail(
            state, "WinHTTP could not open the request (" +
                       std::to_string(GetLastError()) + ")");
    }

    LPCWSTR header_pointer = nullptr;
    DWORD header_length = 0;
    if (!headers.empty()) {
        header_pointer = headers.c_str();
        header_length = static_cast<DWORD>(-1);
    }

    LPVOID body_pointer = nullptr;
    if (!body.empty()) {
        body_pointer = const_cast<char*>(body.data());
    }

    const BOOL sent = WinHttpSendRequest(
        client.request,
        header_pointer,
        header_length,
        body_pointer,
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);

    if (!sent || !WinHttpReceiveResponse(client.request, nullptr)) {
        return request_fail(
            state, "WinHTTP request failed (" + std::to_string(GetLastError()) + ")");
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(
        client.request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);

    std::wstring status_text;
    query_wide_string(client.request, WINHTTP_QUERY_STATUS_TEXT, status_text);

    std::wstring raw_headers;
    query_wide_string(client.request, WINHTTP_QUERY_RAW_HEADERS_CRLF, raw_headers);
    const std::string header_text = util::narrow(raw_headers);

    std::string response_body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(client.request, &available) || available == 0) {
            break;
        }

        std::string chunk(available, '\0');
        DWORD received = 0;
        if (!WinHttpReadData(
                client.request, chunk.data(), available, &received) ||
            received == 0) {
            break;
        }

        chunk.resize(received);
        response_body += chunk;
    }

    lua_newtable(state);
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, "Success");
    lua_pushboolean(state, status >= 200 && status < 300 ? 1 : 0);
    lua_setfield(state, -2, "Ok");
    lua_pushinteger(state, static_cast<int>(status));
    lua_setfield(state, -2, "StatusCode");

    const std::string status_message = util::narrow(status_text);
    lua_pushlstring(state, status_message.data(), status_message.size());
    lua_setfield(state, -2, "StatusMessage");

    lua_pushlstring(state, response_body.data(), response_body.size());
    lua_setfield(state, -2, "Body");
    lua_pushlstring(state, header_text.data(), header_text.size());
    lua_setfield(state, -2, "RawHeaders");

    push_header_table(state, header_text);
    lua_setfield(state, -2, "Headers");

    return 1;
}

}

bool install(lua_State* state) {
    if (!lua_checkstack(state, 16)) {
        return false;
    }

    const int top = lua_gettop(state);

    lua_rawgeti(state, LUA_REGISTRYINDEX, registry_genv);
    if (lua_type(state, -1) != LUA_TTABLE) {
        lua_settop(state, top);
        lua_newtable(state);
        lua_pushvalue(state, -1);
        lua_rawseti(state, LUA_REGISTRYINDEX, registry_genv);
    }
    lua_settop(state, top);

    const struct binding {
        const char* name;
        lua_CFunction function;
    } bindings[] = {
        {"getgenv", api_getgenv},
        {"getrenv", api_getrenv},
        {"identifyexecutor", api_identify},
        {"getexecutorname", api_executor_name},
        {"checkcaller", api_checkcaller},
        {"iscclosure", api_iscclosure},
        {"islclosure", api_islclosure},
        {"newcclosure", api_newcclosure},
        {"loadstring", api_loadstring},
        {"hookmetamethod", api_hookmetamethod},
        {"getrawmetatable", api_getrawmetatable},
        {"getnamecallmethod", api_getnamecallmethod},
        {"setclipboard", api_setclipboard},
        {"queue_on_teleport", api_queue_on_teleport},
        {"request", api_request},
        {"http_request", api_request},
    };

    for (const auto& binding : bindings) {
        lua_pushcclosure(state, binding.function, nullptr, 0);
        lua_setglobal(state, binding.name);
    }

    lua_newtable(state);
    lua_pushcclosure(state, api_request, nullptr, 0);
    lua_setfield(state, -2, "request");
    lua_pushcclosure(state, api_request, nullptr, 0);
    lua_setfield(state, -2, "http_request");
    lua_setglobal(state, "syn");

    lua_pushboolean(state, 1);
    lua_rawseti(state, LUA_REGISTRYINDEX, registry_marker);

    lua_settop(state, top);
    return true;
}

bool installed(lua_State* state) {
    const int top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, registry_marker);
    const bool value = lua_toboolean(state, -1) != 0;
    lua_settop(state, top);
    return value;
}

}
