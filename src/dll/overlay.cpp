#include "overlay.hpp"

#include "hooks.hpp"
#include "log.hpp"
#include "state.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <MinHook.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace xeno::overlay {
namespace {

constexpr const char* default_editor =
    "-- Xeno workspace\n"
    "local Players = game:GetService(\"Players\")\n"
    "\n"
    "print(\"executor:\", identifyexecutor())\n"
    "for _, player in Players:GetPlayers() do\n"
    "    print(player.Name, player.DisplayName)\n"
    "end\n";

struct overlay_state {
    std::atomic<bool> visible{true};
    std::atomic<bool> initialized{false};
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HWND window = nullptr;
    WNDPROC previous_wndproc = nullptr;

    using present_fn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
    using resize_fn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT);

    present_fn present = nullptr;
    resize_fn resize = nullptr;

    char editor[64 * 1024]{};
};

overlay_state& instance() {
    static overlay_state value;
    return value;
}

struct window_search {
    DWORD process_id = 0;
    HWND best = nullptr;
    LONG area = 0;
};

BOOL CALLBACK enum_window(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<window_search*>(parameter);

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search->process_id || !IsWindowVisible(window)) {
        return TRUE;
    }

    RECT client{};
    if (!GetClientRect(window, &client)) {
        return TRUE;
    }

    const LONG area = (client.right - client.left) * (client.bottom - client.top);
    if (area > search->area) {
        search->area = area;
        search->best = window;
    }
    return TRUE;
}

HWND find_game_window() {
    window_search search;
    search.process_id = GetCurrentProcessId();
    EnumWindows(enum_window, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

bool query_render_addresses(void*& present, void*& resize) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"XenoProbeWindow";
    RegisterClassExW(&window_class);

    HWND window = CreateWindowExW(
        0,
        window_class.lpszClassName,
        L"Xeno",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        64,
        64,
        nullptr,
        nullptr,
        window_class.hInstance,
        nullptr);

    if (!window) {
        UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 1;
    description.BufferDesc.Width = 64;
    description.BufferDesc.Height = 64;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapchain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL feature_level{};

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &description,
        &swapchain,
        &device,
        &feature_level,
        &context);

    if (FAILED(result)) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &description,
            &swapchain,
            &device,
            &feature_level,
            &context);
    }

    if (FAILED(result) || !swapchain) {
        DestroyWindow(window);
        UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(swapchain);
    present = vtable[8];
    resize = vtable[13];

    if (context) {
        context->Release();
    }
    if (device) {
        device->Release();
    }
    swapchain->Release();

    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);

    return present != nullptr && resize != nullptr;
}

void apply_theme() {
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.TabRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);

    ImVec4* colors = style.Colors;
    const ImVec4 background(0.047f, 0.051f, 0.071f, 0.98f);
    const ImVec4 surface(0.078f, 0.086f, 0.114f, 1.0f);
    const ImVec4 surface_hover(0.114f, 0.125f, 0.161f, 1.0f);
    const ImVec4 accent(0.588f, 0.435f, 1.0f, 1.0f);
    const ImVec4 accent_hover(0.675f, 0.545f, 1.0f, 1.0f);
    const ImVec4 text(0.882f, 0.902f, 0.941f, 1.0f);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.51f, 0.59f, 1.0f);
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = ImVec4(0.039f, 0.043f, 0.06f, 1.0f);
    colors[ImGuiCol_PopupBg] = background;
    colors[ImGuiCol_Border] = ImVec4(0.16f, 0.18f, 0.24f, 1.0f);
    colors[ImGuiCol_FrameBg] = surface;
    colors[ImGuiCol_FrameBgHovered] = surface_hover;
    colors[ImGuiCol_FrameBgActive] = surface_hover;
    colors[ImGuiCol_TitleBg] = background;
    colors[ImGuiCol_TitleBgActive] = background;
    colors[ImGuiCol_TitleBgCollapsed] = background;
    colors[ImGuiCol_MenuBarBg] = background;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.031f, 0.035f, 0.051f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = surface_hover;
    colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    colors[ImGuiCol_ScrollbarGrabActive] = accent_hover;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_hover;
    colors[ImGuiCol_Button] = surface;
    colors[ImGuiCol_ButtonHovered] = surface_hover;
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = surface;
    colors[ImGuiCol_HeaderHovered] = surface_hover;
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Separator] = ImVec4(0.13f, 0.15f, 0.2f, 1.0f);
    colors[ImGuiCol_Tab] = surface;
    colors[ImGuiCol_TabHovered] = accent;
    colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.16f, 0.33f, 1.0f);
    colors[ImGuiCol_TabDimmed] = surface;
    colors[ImGuiCol_TabDimmedSelected] = surface_hover;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.32f, 0.24f, 0.54f, 0.8f);
}

void draw_editor() {
    auto& state = instance();

    ImGui::TextDisabled("Luau workspace");
    ImGui::SameLine();
    if (xeno::state::attached()) {
        ImGui::TextColored(ImVec4(0.36f, 0.85f, 0.55f, 1.0f), "connected");
    } else {
        ImGui::TextColored(ImVec4(0.93f, 0.71f, 0.32f, 1.0f), "waiting for state");
    }

    ImGui::InputTextMultiline(
        "##workspace",
        state.editor,
        sizeof(state.editor),
        ImVec2(-1.0f, 300.0f),
        ImGuiInputTextFlags_AllowTabInput);

    const bool ready = xeno::state::attached() && state.editor[0] != '\0';
    if (!ready) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Execute", ImVec2(150.0f, 32.0f))) {
        const size_t length = std::strlen(state.editor);
        if (xeno::state::schedule(state.editor)) {
            xeno::log::info("queued script (" + std::to_string(length) + " bytes)");
        }
    }

    if (!ready) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(90.0f, 32.0f))) {
        state.editor[0] = '\0';
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Enter executes from the editor");

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (ready) {
            xeno::state::schedule(state.editor);
            xeno::log::info("queued script (shortcut)");
        }
    }
}

void draw_console() {
    static bool auto_scroll = true;

    if (ImGui::Button("Clear")) {
        xeno::log::clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &auto_scroll);
    ImGui::Separator();

    ImGui::BeginChild(
        "##log",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_HorizontalScrollbar);

    const auto entries = xeno::log::snapshot();
    for (const auto& entry : entries) {
        ImVec4 color(0.80f, 0.83f, 0.89f, 1.0f);
        if (entry.kind == xeno::log::level::warn) {
            color = ImVec4(0.93f, 0.71f, 0.32f, 1.0f);
        } else if (entry.kind == xeno::log::level::error) {
            color = ImVec4(0.94f, 0.38f, 0.42f, 1.0f);
        }

        ImGui::TextColored(
            color, "%s  %s", entry.time.c_str(), entry.text.c_str());
    }

    if (auto_scroll) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void draw_settings() {
    const auto autoexec = util::autoexec_dir();
    const auto config = util::xeno_dir() / L"signatures.ini";
    const std::string autoexec_text = util::narrow(autoexec.wstring());
    const std::string config_text = util::narrow(config.wstring());

    ImGui::TextDisabled("Menu");
    ImGui::Text("INSERT toggles the overlay.");
    ImGui::Separator();

    ImGui::TextDisabled("Autoexec");
    ImGui::TextWrapped("%s", autoexec_text.c_str());
    if (ImGui::Button("Open autoexec folder")) {
        ShellExecuteW(
            nullptr, L"open", autoexec.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::TextDisabled("Files placed here run on the next attach.");
    ImGui::Separator();

    ImGui::TextDisabled("Signatures");
    ImGui::TextWrapped("%s", config_text.c_str());
    ImGui::TextDisabled("Override lua_resume when a Roblox update rotates bytes.");
}

void draw_about() {
    ImGui::Text("%s %s", XENO_NAME, XENO_VERSION);
    ImGui::TextDisabled("%s", XENO_TAGLINE);
    ImGui::Separator();
    ImGui::TextWrapped(
        "Internal Luau runtime: manual-map injector, resume-scheduler execution, "
        "ImGui workspace, and an executor API registered directly into the game "
        "global environment.");
    ImGui::Separator();
    ImGui::Text("Luau state: %s", xeno::state::attached() ? "attached" : "detached");
    ImGui::Text("Build: %s %s", __DATE__, __TIME__);
    ImGui::Text("License: MIT");
}

void draw_window() {
    const bool attached = xeno::state::attached();
    const ImVec2 center = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowSize(ImVec2(820.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(center.x * 0.5f, center.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    ImGui::Begin(
        "Xeno###xeno_window",
        nullptr,
        ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.50f, 1.0f, 1.0f));
    ImGui::Text("%s", XENO_NAME);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", XENO_VERSION);
    ImGui::SameLine(ImGui::GetWindowWidth() - 210.0f);
    if (attached) {
        ImGui::TextColored(ImVec4(0.36f, 0.85f, 0.55f, 1.0f), "state online");
    } else {
        ImGui::TextColored(ImVec4(0.93f, 0.71f, 0.32f, 1.0f), "waiting for state");
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Editor")) {
            draw_editor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            draw_console();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            draw_settings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("About")) {
            draw_about();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void render_frame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (instance().visible.load()) {
        draw_window();
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto& state = instance();

    if (state.initialized.load() && message == WM_KEYDOWN && wparam == VK_INSERT) {
        if ((lparam & (1LL << 30)) == 0) {
            state.visible.store(!state.visible.load());
        }
    }

    if (state.initialized.load() && state.visible.load()) {
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        const ImGuiIO& io = ImGui::GetIO();

        if (io.WantCaptureMouse &&
            message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
            return 1;
        }
        if (io.WantCaptureKeyboard &&
            message >= WM_KEYFIRST && message <= WM_KEYLAST) {
            return 1;
        }
    }

    if (!state.previous_wndproc) {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    return CallWindowProcW(state.previous_wndproc, window, message, wparam, lparam);
}

bool initialize_overlay(IDXGISwapChain* swapchain) {
    static std::mutex init_mutex;
    std::lock_guard lock(init_mutex);

    auto& state = instance();
    if (state.initialized.load()) {
        return true;
    }

    if (FAILED(swapchain->GetDevice(
            __uuidof(ID3D11Device), reinterpret_cast<void**>(&state.device)))) {
        return false;
    }

    state.device->GetImmediateContext(&state.context);

    DXGI_SWAP_CHAIN_DESC description{};
    swapchain->GetDesc(&description);
    state.window = description.OutputWindow ? description.OutputWindow : find_game_window();
    if (!state.window) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.08f;

    apply_theme();
    ImGui_ImplWin32_Init(state.window);
    ImGui_ImplDX11_Init(state.device, state.context);

    SetLastError(0);
    state.previous_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        state.window,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&window_proc)));
    if (!state.previous_wndproc) {
        xeno::log::warn("could not subclass the game window");
    }

    std::strncpy(state.editor, default_editor, sizeof(state.editor) - 1);
    state.initialized.store(true);

    xeno::log::info("overlay attached to game window");
    return true;
}

HRESULT WINAPI present_detour(IDXGISwapChain* swapchain, UINT sync, UINT flags) {
    auto& state = instance();

    if (!state.initialized.load() && !initialize_overlay(swapchain)) {
        return state.present ? state.present(swapchain, sync, flags) : S_OK;
    }

    render_frame();
    return state.present ? state.present(swapchain, sync, flags) : S_OK;
}

HRESULT WINAPI resize_detour(
    IDXGISwapChain* swapchain, UINT count, UINT width, UINT height, UINT flags) {
    auto& state = instance();
    if (state.initialized.load()) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }

    const HRESULT result = state.resize
        ? state.resize(swapchain, count, width, height, flags)
        : S_OK;

    if (state.initialized.load() && SUCCEEDED(result)) {
        ImGui_ImplDX11_CreateDeviceObjects();
    }

    return result;
}

}

void start() {
    std::thread([] {
        void* present = nullptr;
        void* resize = nullptr;

        for (int attempt = 0; attempt < 30; ++attempt) {
            if (query_render_addresses(present, resize)) {
                break;
            }
            Sleep(1000);
        }

        if (!present) {
            xeno::log::error("could not resolve IDXGISwapChain::Present");
            return;
        }

        std::lock_guard lock(hooks::mutex());

        const MH_STATUS initialized = MH_Initialize();
        if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
            xeno::log::error("MinHook initialization failed for the overlay");
            return;
        }

        auto& state = instance();

        if (MH_CreateHook(
                present,
                reinterpret_cast<LPVOID>(&present_detour),
                reinterpret_cast<LPVOID*>(&state.present)) != MH_OK) {
            xeno::log::error("could not hook IDXGISwapChain::Present");
            return;
        }

        if (MH_CreateHook(
                resize,
                reinterpret_cast<LPVOID>(&resize_detour),
                reinterpret_cast<LPVOID*>(&state.resize)) != MH_OK) {
            xeno::log::warn("could not hook IDXGISwapChain::ResizeBuffers");
        }

        MH_EnableHook(present);
        if (state.resize) {
            MH_EnableHook(resize);
        }

        xeno::log::info("render hooks installed");
    }).detach();
}

}
