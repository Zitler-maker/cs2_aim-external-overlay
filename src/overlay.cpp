#include "overlay.h"
#include "mem.h"

#include <d3d11.h>
#include <imgui.h>
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay {

HWND hwnd = nullptr;
static bool g_menuOpen = false;

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_sc = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return 0;
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static bool initD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width = (UINT)GetSystemMetrics(SM_CXSCREEN);
    sd.BufferDesc.Height = (UINT)GetSystemMetrics(SM_CYSCREEN);
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &fl, 1, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, nullptr, &g_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* back = nullptr;
    g_sc->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return false;
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return true;
}

static void setClickable(bool on) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (on) ex &= ~WS_EX_TRANSPARENT;
    else ex |= WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
}

static void drawEsp() {
    GameSnap s;
    {
        std::lock_guard<std::mutex> lk(g_snapMtx);
        s = g_snap;
    }
    if (!g_set.esp) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (int i = 0; i < s.count; i++) {
        const PlayerSnap& p = s.players[i];
        float h = p.feetScr.y - p.headScr.y;
        if (h < 2.f) continue;
        float w = h * 0.5f;
        float x0 = p.headScr.x - w * 0.5f;
        float y0 = p.headScr.y;
        ImU32 col = ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.9f));
        dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), col, 0.f, 0, 1.5f);

        float hpW = 4.f;
        float frac = (float)p.hp / 100.f;
        ImU32 hpCol = ImGui::GetColorU32(ImVec4(1.f - frac, frac, 0.f, 1.f));
        dl->AddRectFilled(ImVec2(x0 - hpW - 2.f, y0), ImVec2(x0 - 2.f, y0 + h * frac), hpCol);
        dl->AddRect(ImVec2(x0 - hpW - 2.f, y0), ImVec2(x0 - 2.f, y0 + h), ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.8f)), 0.f, 0, 1.f);
    }
}

static void drawMenu() {
    if (!g_menuOpen) return;
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("panel", &g_menuOpen, ImGuiWindowFlags_NoCollapse);
    ImGui::Checkbox("aim assist", &g_set.aimbot);
    ImGui::Checkbox("vision", &g_set.esp);
    ImGui::SliderFloat("fov", &g_set.fov, 1.f, 30.f, "%.1f");
    ImGui::SliderFloat("smooth", &g_set.smooth, 1.f, 5.f, "%.1f");
    ImGui::Text("hold mouse5 to aim");
    ImGui::Text("insert toggles menu");
    ImGui::End();
}

int run(HINSTANCE hInst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RendererWindow";
    RegisterClassExW(&wc);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"Renderer", WS_POPUP,
        0, 0, w, h, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    if (!initD3D()) return 2;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOMOVE | SWP_NOSIZE);

    MSG msg{};
    bool insertWasDown = false;
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) goto out;
        }

        bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (insertDown && !insertWasDown) {
            g_menuOpen = !g_menuOpen;
            setClickable(g_menuOpen);
            if (g_menuOpen) SetForegroundWindow(hwnd);
        }
        insertWasDown = insertDown;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (!IsWindow(hwnd)) {
            hwnd = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
                wc.lpszClassName, L"Renderer", WS_POPUP,
                0, 0, w, h, nullptr, nullptr, hInst, nullptr);
            if (!hwnd) goto out;
            SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
            if (g_sc) g_sc->Release();
            if (g_rtv) g_rtv->Release();
            g_sc = nullptr;
            g_rtv = nullptr;
            initD3D();
            ImGui_ImplWin32_Shutdown();
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Init(hwnd);
            ImGui_ImplDX11_Init(g_dev, g_ctx);
            ShowWindow(hwnd, SW_SHOW);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, w, h, SWP_NOMOVE | SWP_NOSIZE);
            setClickable(g_menuOpen);
        }

        drawEsp();
        drawMenu();

        ImGui::Render();
        const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_sc->Present(1, 0);
    }
out:
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_rtv) g_rtv->Release();
    if (g_sc) g_sc->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    DestroyWindow(hwnd);
    return 0;
}

bool menuOpen() { return g_menuOpen; }

}
