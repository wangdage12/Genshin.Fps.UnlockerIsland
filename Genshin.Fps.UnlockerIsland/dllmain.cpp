#include "pch.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Menu_T
{
    bool showGui = true;
    bool show_fps = true;
    bool show_fps_use_floating_window = false;
    int toggleKey = VK_HOME;
    bool waitingForKey = false;

    enum class FpsCorner
    {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    int selected_fps = 60;
    int fps_index = 2;
    float fov_value = 90.0f;
    bool enable_fps_override = false;
    bool enable_fov_override = false;
    bool enable_display_fog_override = false;
    bool enable_Perspective_override = false;
    bool enable_syncount_override = true;

};
std::vector<Menu_T::FpsCorner> selected_corners = { Menu_T::FpsCorner::TopLeft };

extern Menu_T menu;

Menu_T menu = {};

namespace Gui
{
    typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
    typedef HRESULT(__stdcall* Present1_t)(IDXGISwapChain*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void ControllerProc();
    void RenderMenuWindow();

    WNDPROC oWndProc = nullptr;
    Present_t oPresent = nullptr;
    Present1_t oPresent1 = nullptr;
    ResizeBuffers_t oResizeBuffers = nullptr;

    HWND g_hWnd = nullptr;
    HWND g_MenuWindow = nullptr;
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dContext = nullptr;
    ID3D11RenderTargetView* g_mainRTV = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    bool g_ImGuiInitialized = false;
    bool g_MenuWindowInitialized = false;

    void CleanupRenderTarget()
    {
        if (g_mainRTV) {
            g_mainRTV->Release();
            g_mainRTV = nullptr;
        }
    }

    void CreateRenderTarget(IDXGISwapChain* pSwapChain)
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
            HRESULT hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRTV);
            if (SUCCEEDED(hr))
            {
                LOG_INFO("Render target view created successfully");
            }
            else
            {
                LOG_ERROR("Failed to create render target view, HRESULT: " + std::to_string(hr));
            }
            pBackBuffer->Release();
        }
        else
        {
            LOG_ERROR("Failed to get back buffer");
        }
    }

    void SetNiceStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.FrameRounding = 5.0f;
        style.GrabRounding = 5.0f;
        style.ScrollbarRounding = 5.0f;

        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.95f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.70f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.6f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1f, 0.35f, 0.55f, 1.0f);
    }


    void InitImGui(IDXGISwapChain* pSwapChain)
    {
        if (g_ImGuiInitialized) return;

        LOG_FUNCTION("InitImGui", "Initializing ImGui");
        
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
            LOG_INFO("Got D3D11 device successfully");
            
            g_pd3dDevice->GetImmediateContext(&g_pd3dContext);

            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_hWnd = sd.OutputWindow;
            
            LOG_INFO("Game window handle: " + std::to_string((uintptr_t)g_hWnd));

            CreateRenderTarget(pSwapChain);
            
            LOG_INFO("Render target created");

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

            ImVector<ImWchar> ranges;
            ImFontGlyphRangesBuilder builder;

            builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
            builder.BuildRanges(&ranges);

            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", 20.0f, NULL, ranges.Data);
            io.Fonts->Build();
            
            LOG_INFO("Font loaded successfully");

            ImGui::StyleColorsDark();
            SetNiceStyle();

            ImGui_ImplWin32_Init(g_hWnd);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

            oWndProc = (WNDPROC)SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
            
            LOG_INFO("WndProc hooked successfully");
            LOG_INFO("ImGui initialized successfully");
            g_ImGuiInitialized = true;
        }
        else
        {
            LOG_ERROR("Failed to get D3D11 device for ImGui initialization");
        }
    }

    const char* GetCornerName(Menu_T::FpsCorner corner)
    {
        switch (corner)
        {
        case Menu_T::FpsCorner::TopLeft: return u8"左上角";
        case Menu_T::FpsCorner::TopRight: return u8"右上角";
        case Menu_T::FpsCorner::BottomLeft: return u8"左下角";
        case Menu_T::FpsCorner::BottomRight: return u8"右下角";
        default: return "Unknown";
        }
    }

    ImVec2 GetCornerPos(Menu_T::FpsCorner corner, const ImVec2& text_size)
    {
        ImVec2 pos;
        auto display_size = ImGui::GetIO().DisplaySize;

        switch (corner)
        {
        case Menu_T::FpsCorner::TopLeft:
            pos = ImVec2(5.f, 5.f);
            break;
        case Menu_T::FpsCorner::TopRight:
            pos = ImVec2(display_size.x - text_size.x - 5.f, 5.f);
            break;
        case Menu_T::FpsCorner::BottomLeft:
            pos = ImVec2(5.f, display_size.y - text_size.y - 5.f);
            break;
        case Menu_T::FpsCorner::BottomRight:
            pos = ImVec2(display_size.x - text_size.x - 5.f, display_size.y - text_size.y - 5.f);
            break;
        }
        return pos;
    }

    // 保存
    void MenuSaveConfig(const char* filename) {
        LOG_FUNCTION("MenuSaveConfig", "Saving configuration to file");
        
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file for writing");
            return;
        }

        file.write(reinterpret_cast<const char*>(&menu), sizeof(menu));

        // 保存 vector 长度
        size_t count = selected_corners.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));

        // 保存 vector 内容
        for (auto& corner : selected_corners) {
            int val = static_cast<int>(corner);
            file.write(reinterpret_cast<const char*>(&val), sizeof(val));
        }

        file.close();
        LOG_INFO("Configuration saved successfully");
    }


    // 加载
    void MenuLoadConfig(const char* filename) {
        LOG_FUNCTION("MenuLoadConfig", "Loading configuration from file");
        
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            LOG_WARNING("Config file not found or cannot be opened");
            return;
        }

        file.read(reinterpret_cast<char*>(&menu), sizeof(menu));

        // 读取 vector 长度
        size_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        selected_corners.clear();
        for (size_t i = 0; i < count; ++i) {
            int val = 0;
            file.read(reinterpret_cast<char*>(&val), sizeof(val));
            selected_corners.push_back(static_cast<Menu_T::FpsCorner>(val));
        }

        file.close();
        LOG_INFO("Configuration loaded successfully");
    }

    const char* GetKeyName(int vkCode) {
        static char name[128] = "Unknown";
        UINT scanCode = MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC);
        switch (vkCode) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scanCode |= 0x100; break;
        }
        if (GetKeyNameTextA(scanCode << 16, name, sizeof(name)) == 0) {
            strcpy_s(name, "Unknown");
        }
        return name;
    }


    CHAR RenderBuff[4096] = { 0 };
    static int frameCount = 0;
    HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT sync, UINT flags)
    {
        frameCount++;
        if (frameCount % 60 == 0)
        {
            LOG_INFO("HookedPresent called, frame: " + std::to_string(frameCount) + ", ImGui initialized: " + std::string(g_ImGuiInitialized ? "true" : "false"));
        }

        ControllerProc();

        if (!g_ImGuiInitialized)
            InitImGui(pSwapChain);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (menu.show_fps)
        {
            if (menu.show_fps_use_floating_window)
            {
                static ImVec2 fps_window_pos = ImVec2(100, 100); // 初始位置，可记忆
                ImGui::SetNextWindowBgAlpha(0.3f);               // 背景透明
                ImGui::SetNextWindowSize(ImVec2(100, 40), ImGuiCond_Once); // 初始尺寸

                if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    ImGui::SetNextWindowPos(fps_window_pos, ImGuiCond_FirstUseEver);

                ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoSavedSettings;

                ImGui::Begin("FPS窗口", nullptr, flags);

                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

                if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    fps_window_pos = ImGui::GetWindowPos(); // 记录拖动后的位置

                ImGui::End();
            }
            else
            {
                char buffer[64];
                sprintf(buffer, "FPS: %.0f", ImGui::GetIO().Framerate);

                ImDrawList* draw_list = ImGui::GetForegroundDrawList();
                ImVec2 text_size = ImGui::CalcTextSize(buffer);

                for (const auto& corner : selected_corners)
                {
                    ImVec2 pos = GetCornerPos(corner, text_size);
                    draw_list->AddText(pos, IM_COL32_WHITE, buffer);
                }
            }

        }

        if (menu.showGui)
        {
            ImGui::SetNextWindowSize(ImVec2(300, 780), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoNav
                | ImGuiWindowFlags_NoBringToFrontOnFocus;

            ImGui::Begin("MainWindow", nullptr, window_flags);
            {
                ImVec2 window_size = ImGui::GetWindowSize();
                ImGui::SetCursorPosX((window_size.x - ImGui::CalcTextSize(u8"Hello,Genshin Tools!").x) * 0.5f);
                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), u8"Hello,Genshin Tools!");
                ImGui::Separator();
            }

            ImGui::Spacing(); ImGui::Spacing();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            if (ImGui::CollapsingHeader(u8"视觉设置", flags))
            {
                static const char* fps_options[] = {
                    "30", "45", "60", "90", "120", "144", "240", "360", "480",
                    "600", "720", "840", "960", "1080", "2147483647"
                };
                ImGui::Checkbox(u8"启用帧数调节", &menu.enable_fps_override);
                ImGui::Text(u8"帧数选择:");
                ImGui::SameLine();
                if (ImGui::BeginCombo("##fps_combo", fps_options[menu.fps_index]))
                {
                    for (int i = 0; i < IM_ARRAYSIZE(fps_options); ++i)
                    {
                        bool is_selected = (menu.fps_index == i);
                        if (ImGui::Selectable(fps_options[i], is_selected))
                        {
                            menu.fps_index = i;
                            menu.selected_fps = std::atoi(fps_options[i]);
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::DragInt(u8"帧数数值", &menu.selected_fps, 1, 1, 1000, u8"%d FPS");

                ImGui::Checkbox(u8"突破显示器帧数上限", &menu.enable_syncount_override);

                ImGui::Checkbox(u8"启用视角 FOV 修改", &menu.enable_fov_override);
                ImGui::DragFloat(u8"视角 FOV", &menu.fov_value, 0.1f, 30.0f, 150.0f, u8"%.1f°");

                ImGui::Checkbox(u8"启用去雾霾", &menu.enable_display_fog_override);

                ImGui::Checkbox(u8"启用去虚化", &menu.enable_Perspective_override);
            }

            if (ImGui::CollapsingHeader(u8"帧数显示", flags))
            {
                ImGui::Checkbox(u8"显示 FPS", &menu.show_fps);
                ImGui::Checkbox(u8"使显示窗口可移动", &menu.show_fps_use_floating_window);

                ImGui::Text(u8"显示位置:");
                ImGui::Indent();
                for (int i = 0; i < 4; ++i)
                {
                    bool selected = std::find(selected_corners.begin(), selected_corners.end(), static_cast<Menu_T::FpsCorner>(i)) != selected_corners.end();
                    bool checkbox = selected;
                    if (ImGui::Checkbox(GetCornerName(static_cast<Menu_T::FpsCorner>(i)), &checkbox))
                    {
                        if (checkbox)
                            selected_corners.push_back(static_cast<Menu_T::FpsCorner>(i));
                        else
                            selected_corners.erase(std::remove(selected_corners.begin(), selected_corners.end(), static_cast<Menu_T::FpsCorner>(i)), selected_corners.end());
                    }
                }
                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader(u8"菜单设置配置", flags)) {
                ImGui::Text(u8"启动菜单快捷键: %s", GetKeyName(menu.toggleKey));
                if (ImGui::Button(u8"修改快捷键")) {
                    menu.waitingForKey = true;
                }
                if (menu.waitingForKey) {
                    ImGui::Text(u8"请按需要设置的快捷键...");

                    for (int key = 0x01; key <= 0xFE; ++key) {
                        if (GetAsyncKeyState(key) & 0x8000) {
                            menu.toggleKey = key;
                            menu.waitingForKey = false;
                            break;
                        }
                    }
                }
                if (ImGui::Button(u8"保存设置", ImVec2(138, 40)))
                {
                    MenuSaveConfig("C:\\Users\\Genshin.Fps.UnlockerIsland.bin");
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"加载设置", ImVec2(138, 40)))
                {
                    MenuLoadConfig("C:\\Users\\Genshin.Fps.UnlockerIsland.bin");
                }
            }

            ImGui::Text(u8"本程序理论可以自动适配最新版游戏\n如出现报错请联系作者反馈!\n免费项目!请勿非法盈利!");
            if (ImGui::Selectable(u8"作者:哔哩哔哩-柯莱宝贝", false, ImGuiSelectableFlags_DontClosePopups)) {
                ShellExecuteA(NULL, "open", "https://space.bilibili.com/1831941574", NULL, NULL, SW_SHOWNORMAL);
            }

            ImGui::End();
        }


        ImGui::Render();
        
        if (frameCount % 60 == 0)
        {
            LOG_INFO("ImGui rendered, showGui: " + std::string(menu.showGui ? "true" : "false"));
        }
        
        g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        return oPresent(pSwapChain, sync, flags);
    }

    HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain,
        UINT BufferCount, UINT Width, UINT Height,
        DXGI_FORMAT NewFormat, UINT SwapChainFlags)
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        CleanupRenderTarget();

        HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        CreateRenderTarget(pSwapChain);
        ImGui_ImplDX11_CreateDeviceObjects();

        return hr;
    }

    LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {

        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
        if (msg == WM_KEYDOWN && wParam == menu.toggleKey)
        {
            menu.showGui = !menu.showGui;
            if (menu.showGui && g_MenuWindow)
            {
                ShowWindow(g_MenuWindow, SW_SHOW);
                SetWindowPos(g_MenuWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            else if (g_MenuWindow)
            {
                ShowWindow(g_MenuWindow, SW_HIDE);
            }
        }
        return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        case WM_KEYDOWN:
            if (wParam == menu.toggleKey)
            {
                menu.showGui = !menu.showGui;
                if (menu.showGui)
                {
                    ShowWindow(hWnd, SW_SHOW);
                    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
                else
                {
                    ShowWindow(hWnd, SW_HIDE);
                }
                return 0;
            }
            break;
        case WM_SIZE:
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
            {
                ImGui_ImplDX11_InvalidateDeviceObjects();
                CleanupRenderTarget();
                HRESULT hr = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget(g_pSwapChain);
                ImGui_ImplDX11_CreateDeviceObjects();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_CLOSE:
            // 关闭窗口时只是隐藏，不销毁
            menu.showGui = false;
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            // 不发送退出消息，因为窗口不应该被销毁
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    bool CreateMenuWindow()
    {
        if (g_MenuWindowInitialized) return true;

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, MenuWndProc, 0L, 0L,
            GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
            L"Genshin.Fps.UnlockerIslandMenu", NULL };
        RegisterClassExW(&wc);

        g_MenuWindow = CreateWindowExW(
            WS_EX_TOOLWINDOW,  // 工具窗口，不会出现在Alt+Tab中
            wc.lpszClassName,
            L"Genshin FPS Unlocker",
            WS_CAPTION | WS_SYSMENU,  // 仅显示标题栏和关闭按钮，禁用拖动
            100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL);

        if (!g_MenuWindow)
        {
            LOG_ERROR("Failed to create menu window");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = g_MenuWindow;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

        if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &level, &g_pd3dContext)))
        {
            LOG_ERROR("Failed to create D3D11 device for menu window");
            DestroyWindow(g_MenuWindow);
            g_MenuWindow = nullptr;
            return false;
        }

        CreateRenderTarget(g_pSwapChain);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;

        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        builder.BuildRanges(&ranges);

        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", 18.0f, NULL, ranges.Data);
        io.Fonts->Build();

        ImGui::StyleColorsDark();
        SetNiceStyle();

        ImGui_ImplWin32_Init(g_MenuWindow);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

        g_ImGuiInitialized = true;
        g_MenuWindowInitialized = true;

        LOG_INFO("Menu window created successfully");
        return true;
    }

    void RenderMenuWindow()
    {
        if (!g_MenuWindow || !g_MenuWindowInitialized) return;

        // 全局按键检测，即使窗口关闭也能检测到
        static bool keyWasPressed = false;
        bool keyIsPressed = (GetAsyncKeyState(menu.toggleKey) & 0x8000) != 0;
        
        if (keyIsPressed && !keyWasPressed)
        {
            menu.showGui = !menu.showGui;
            if (menu.showGui)
            {
                ShowWindow(g_MenuWindow, SW_SHOW);
                SetWindowPos(g_MenuWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            else
            {
                ShowWindow(g_MenuWindow, SW_HIDE);
            }
        }
        keyWasPressed = keyIsPressed;

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (IsWindowVisible(g_MenuWindow))
        {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (menu.showGui)
            {
                ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

                ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove;

                ImGui::Begin("Genshin FPS Unlocker", nullptr, window_flags);
                {
                    ImVec2 window_size = ImGui::GetWindowSize();
                    ImGui::SetCursorPosX((window_size.x - ImGui::CalcTextSize(u8"Hello,Genshin Tools!").x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), u8"Hello,Genshin Tools!");
                    ImGui::Separator();
                }

                ImGui::Spacing(); ImGui::Spacing();

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                if (ImGui::CollapsingHeader(u8"视觉设置", flags))
                {
                    static const char* fps_options[] = {
                        "30", "45", "60", "90", "120", "144", "240", "360", "480",
                        "600", "720", "840", "960", "1080", "2147483647"
                    };
                    ImGui::Checkbox(u8"启用帧数调节", &menu.enable_fps_override);
                    ImGui::Text(u8"帧数选择:");
                    ImGui::SameLine();
                    if (ImGui::BeginCombo("##fps_combo", fps_options[menu.fps_index]))
                    {
                        for (int i = 0; i < IM_ARRAYSIZE(fps_options); ++i)
                        {
                            bool is_selected = (menu.fps_index == i);
                            if (ImGui::Selectable(fps_options[i], is_selected))
                            {
                                menu.fps_index = i;
                                menu.selected_fps = std::atoi(fps_options[i]);
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::DragInt(u8"帧数数值", &menu.selected_fps, 1, 1, 1000, u8"%d FPS");

                    ImGui::Checkbox(u8"突破显示器帧数上限", &menu.enable_syncount_override);

                    ImGui::Checkbox(u8"启用视角 FOV 修改", &menu.enable_fov_override);
                    ImGui::DragFloat(u8"视角 FOV", &menu.fov_value, 0.1f, 30.0f, 150.0f, u8"%.1f°");

                    ImGui::Checkbox(u8"启用去雾霾", &menu.enable_display_fog_override);

                    ImGui::Checkbox(u8"启用去虚化", &menu.enable_Perspective_override);
                }

                if (ImGui::CollapsingHeader(u8"菜单设置配置", flags)) {
                    ImGui::Text(u8"启动菜单快捷键: %s", GetKeyName(menu.toggleKey));
                    if (ImGui::Button(u8"修改快捷键")) {
                        menu.waitingForKey = true;
                    }
                    if (menu.waitingForKey) {
                        ImGui::Text(u8"请按需要设置的快捷键...");

                        for (int key = 0x01; key <= 0xFE; ++key) {
                            if (GetAsyncKeyState(key) & 0x8000) {
                                menu.toggleKey = key;
                                menu.waitingForKey = false;
                                break;
                            }
                        }
                    }
                    if (ImGui::Button(u8"保存设置", ImVec2(138, 40)))
                    {
                        MenuSaveConfig("C:\\Users\\Genshin.Fps.UnlockerIsland.bin");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"加载设置", ImVec2(138, 40)))
                    {
                        MenuLoadConfig("C:\\Users\\Genshin.Fps.UnlockerIsland.bin");
                    }
                }

                ImGui::Text(u8"本程序理论可以自动适配最新版游戏\n如出现报错请联系作者反馈!\n免费项目!请勿非法盈利!");
                if (ImGui::Selectable(u8"查看该修改版本", false, ImGuiSelectableFlags_DontClosePopups)) {
                    ShellExecuteA(NULL, "open", "https://github.com/wangdage12/Genshin.Fps.UnlockerIsland", NULL, NULL, SW_SHOWNORMAL);
                }

                if (ImGui::Selectable(u8"原作者:哔哩哔哩-柯莱宝贝", false, ImGuiSelectableFlags_DontClosePopups)) {
                    ShellExecuteA(NULL, "open", "https://space.bilibili.com/1831941574", NULL, NULL, SW_SHOWNORMAL);
                }

                ImGui::End();
            }

            ImGui::Render();

            g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, NULL);
            
            // 清除背景，解决拖影问题
            const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            g_pd3dContext->ClearRenderTargetView(g_mainRTV, clear_color_with_alpha);
            
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_pSwapChain->Present(1, 0);
        }
    }
    void ControllerProc()
    {
        XINPUT_STATE state;
        if (XInputGetState(0, &state) == ERROR_SUCCESS)
        {
            // RB+LB+X+十字键上
            bool rbPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
            bool lbPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            bool xPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
            bool dpadUpPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;

            static bool toggleComboPrev = false;
            bool toggleComboNow = rbPressed && lbPressed && xPressed && dpadUpPressed;

            if (toggleComboNow && !toggleComboPrev)
            {
                menu.showGui = !menu.showGui;
            }
            toggleComboPrev = toggleComboNow;

            // RB+LB+X+十字键下
            if (menu.showGui)
            {
                bool dpadDownPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;

                static bool msgBoxComboPrev = false;
                bool msgBoxComboNow = rbPressed && lbPressed && xPressed && dpadDownPressed;

                if (msgBoxComboNow && !msgBoxComboPrev)
                {
                    MenuLoadConfig("C:\\Users\\Genshin.Fps.UnlockerIsland.bin");
                }
                msgBoxComboPrev = msgBoxComboNow;
            }
        }
    }
}

namespace GameHook
{
    uintptr_t hGameModule = 0;

    typedef int(*GameUpdate_t)(__int64 a1, const char* a2);
    GameUpdate_t g_original_GameUpdate = nullptr;

    typedef int(*HookGet_FrameCount_t)();
    HookGet_FrameCount_t g_original_HookGet_FrameCount = nullptr;

    typedef int(*Set_FrameCount_t)(int value);
    Set_FrameCount_t g_original_Set_FrameCount = nullptr;

    typedef int(*Set_SyncCount_t)(bool value);
    Set_SyncCount_t g_original_Set_SyncCount = nullptr;

    typedef int(*HookChangeFOV_t)(__int64 a1, float a2);
    HookChangeFOV_t g_original_HookChangeFOV = nullptr;

    typedef int(*HookDisplayFog_t)(__int64 a1, __int64 a2);
    HookDisplayFog_t g_original_HookDisplayFog = nullptr;

    typedef void* (*HookPlayer_Perspective_t)(void* RCX, float Display, void* R8);
    HookPlayer_Perspective_t g_original_Player_Perspective = nullptr;

    bool GameUpdateInit = false;
    __int64 HookGameUpdate(__int64 a1, const char* a2)
    {
        if (!GameUpdateInit)
        {
            GameUpdateInit = true;
            LOG_FUNCTION("HookGameUpdate", "Game update initialized");
        }
  
        if (menu.enable_fps_override)
        {
            g_original_Set_FrameCount(menu.selected_fps);
        }

        if (menu.enable_syncount_override)
        {
            g_original_Set_SyncCount(false);
        }

        return g_original_GameUpdate(a1, a2);
    }

    int HookGet_FrameCount() {
        int ret = g_original_HookGet_FrameCount();
        if (ret >= 60) return 60;
        else if (ret >= 45) return 45;
        else if (ret >= 30) return 30;
        return ret;
    }

    __int64 HookChangeFOV(__int64 a1, float ChangeFovValue)
    {
        if (menu.enable_fov_override)
        {
            ChangeFovValue = menu.fov_value;
        }

        return g_original_HookChangeFOV(a1, ChangeFovValue);
    }

    __declspec(align(16)) static uint8_t g_fakeFogStruct[64];

    __int64 HookDisplayFog(__int64 a1, __int64 a2)
    {
        if (menu.enable_display_fog_override && a2)
        {
            memcpy(g_fakeFogStruct, (void*)a2, sizeof(g_fakeFogStruct));
            g_fakeFogStruct[0] = 0;
            return g_original_HookDisplayFog(a1, (uintptr_t)g_fakeFogStruct);
        }

        return g_original_HookDisplayFog(a1, a2);
    }

    void* HookPlayer_Perspective(void* RCX, float Display, void* R8)
    {
        if (menu.enable_Perspective_override)
        {
            Display = 1.f;
        }
        return g_original_Player_Perspective(RCX, Display, R8);
    }


    bool InitHook()
    {
        LOG_FUNCTION("InitHook", "Starting hook initialization");
        
        hGameModule = (uintptr_t)GetModuleHandleA(NULL);
        while (
            (hGameModule = (uintptr_t)GetModuleHandleA(NULL)) == NULL) {
            Sleep(1000);
        }

        LOG_INFO("Game module handle obtained");

        void* GameUpdateAddr = (void*)PatternScanner::Scan("E8 ? ? ? ? 48 8D 4C 24 ? 8B F8 FF 15 ? ? ? ? E8 ? ? ? ?");
        GameUpdateAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)GameUpdateAddr);
        if (!GameUpdateAddr) {
            LOG_ERROR("HookGameUpdate search failed!");
            MessageBoxA(nullptr, "HookGameUpdate search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
        }
        if (!MinHookManager::Add(GameUpdateAddr, &HookGameUpdate, (void**)&g_original_GameUpdate)) {
            LOG_ERROR("HookGameUpdate install failed!");
            MessageBoxA(nullptr, "HookGameUpdate install failed!", "MinHook", MB_OK | MB_ICONERROR);
        }
        else
        {
            LOG_HOOK("GameUpdate", "Hooked successfully");
        }

        void* Get_FrameCountAddr = (void*)PatternScanner::Scan("E8 ? ? ? ? 85 C0 7E 0E E8 ? ? ? ? 0F 57 C0 F3 0F 2A C0 EB 08 ?");
        Get_FrameCountAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)Get_FrameCountAddr);
        Get_FrameCountAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)Get_FrameCountAddr);
        if (!Get_FrameCountAddr) {
            LOG_ERROR("HookGet_FrameCount search failed!");
            MessageBoxA(nullptr, "HookGet_FrameCount search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
        }
        if (!MinHookManager::Add(Get_FrameCountAddr, &HookGet_FrameCount, (void**)&g_original_HookGet_FrameCount)) {
            LOG_ERROR("HookGet_FrameCount install failed!");
            MessageBoxA(nullptr, "HookGet_FrameCount install failed!", "MinHook", MB_OK | MB_ICONERROR);
        }
        else
        {
            LOG_HOOK("Get_FrameCount", "Hooked successfully");
        }

        void* Set_FrameCountAddr = (void*)PatternScanner::Scan("E8 ? ? ? ? E8 ? ? ? ? 83 F8 1F 0F 9C 05 ? ? ? ? 48 8B 05 ? ? ? ? ");
        Set_FrameCountAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)Set_FrameCountAddr);
        Set_FrameCountAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)Set_FrameCountAddr);
        if (!Set_FrameCountAddr) {
            LOG_ERROR("Set_FrameCountAddr search failed!");
            MessageBoxA(nullptr, "Set_FrameCountAddr search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
        }
        g_original_Set_FrameCount = (Set_FrameCount_t)Set_FrameCountAddr;
        LOG_HOOK("Set_FrameCount", "Address resolved successfully");

        void* Set_SyncCountAddr = (void*)PatternScanner::Scan("E8 ? ? ? ? E8 ? ? ? ? 89 C6 E8 ? ? ? ? 31 C9 89 F2 49 89 C0 E8 ? ? ? ? 48 89 C6 48 8B 0D ? ? ? ? 80 B9 ? ? ? ? ? 74 47 48 8B 3D ? ? ? ? 48 85 DF 74 4C ");
        Set_SyncCountAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)Set_SyncCountAddr);
        if (!Set_SyncCountAddr) {
            LOG_ERROR("Set_SyncCountAddr search failed!");
            MessageBoxA(nullptr, "Set_SyncCountAddr search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
            exit(0);
            ExitProcess(0);
        }
        g_original_Set_SyncCount = (Set_SyncCount_t)Set_SyncCountAddr;
        LOG_HOOK("Set_SyncCount", "Address resolved successfully");

        void* ChangeFOVAddr = (void*)PatternScanner::Scan("40 53 48 83 EC 60 0F 29 74 24 ? 48 8B D9 0F 28 F1 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 48 8B C8 ");
        if (!ChangeFOVAddr) {
            LOG_ERROR("HookChangeFOV search failed!");
            MessageBoxA(nullptr, "HookChangeFOV search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
        }
        if (!MinHookManager::Add(ChangeFOVAddr, &HookChangeFOV, (void**)&g_original_HookChangeFOV)) {
            LOG_ERROR("HookChangeFOV install failed!");
            MessageBoxA(nullptr, "HookChangeFOV install failed!", "MinHook", MB_OK | MB_ICONERROR);
        }
        else
        {
            LOG_HOOK("ChangeFOV", "Hooked successfully");
        }

        void* DisplayFogAddr = (void*)PatternScanner::Scan("0F B6 02 88 01 8B 42 04 89 41 04 F3 0F 10 52 ? F3 0F 10 4A ? F3 0F 10 42 ? 8B 42 08 ");
        if (!DisplayFogAddr) {
            LOG_ERROR("HookDisplayFog search failed!");
            MessageBoxA(nullptr, "HookDisplayFog search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
        }
        if (!MinHookManager::Add(DisplayFogAddr, &HookDisplayFog, (void**)&g_original_HookDisplayFog)) {
            LOG_ERROR("HookDisplayFog install failed!");
            MessageBoxA(nullptr, "HookDisplayFog install failed!", "MinHook", MB_OK | MB_ICONERROR);
        }
        else
        {
            LOG_HOOK("DisplayFog", "Hooked successfully");
        }

        void* Player_PerspectiveAddr = (void*)PatternScanner::Scan("E8 ? ? ? ? 48 8B BE ? ? ? ? 80 3D ? ? ? ? ? 0F 85 ? ? ? ? 80 BE ? ? ? ? ? 74 11");
        Player_PerspectiveAddr = (void*)PatternScanner::ResolveRelativeAddress((uintptr_t)Player_PerspectiveAddr);
        if (!Player_PerspectiveAddr) {
            LOG_ERROR("HookPlayer_Perspective search failed!");
            MessageBoxA(nullptr, "HookPlayer_Perspective search failed!", "PatternScanner", MB_OK | MB_ICONERROR);
        }
        if (!MinHookManager::Add(Player_PerspectiveAddr, &HookPlayer_Perspective, (void**)&g_original_Player_Perspective)) {
            LOG_ERROR("HookPlayer_Perspective install failed!");
            MessageBoxA(nullptr, "HookPlayer_Perspective install failed!", "MinHook", MB_OK | MB_ICONERROR);
        }
        else
        {
            LOG_HOOK("Player_Perspective", "Hooked successfully");
        }

        LOG_INFO("All hooks initialized successfully");
        return true;
    }
}

DWORD WINAPI Run(LPVOID lpParam)
{
    LOG_FUNCTION("Run", "Starting initialization thread");
    
    GameHook::InitHook();
    while (!GameHook::GameUpdateInit)
    {
        Sleep(1000);
    }
    
    LOG_INFO("Game update initialized, creating menu window");
    
    if (!Gui::CreateMenuWindow())
    {
        LOG_ERROR("Failed to create menu window");
        return FALSE;
    }

    if (menu.showGui)
    {
        ShowWindow(Gui::g_MenuWindow, SW_SHOW);
        SetWindowPos(Gui::g_MenuWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    LOG_INFO("menu.showGui initial value: " + std::string(menu.showGui ? "true" : "false"));
    LOG_FUNCTION("Run", "Initialization completed");

    while (true)
    {
        Gui::RenderMenuWindow();
        Sleep(1);
    }

    return TRUE;
}


BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        
        wchar_t logPath[MAX_PATH];
        GetTempPathW(MAX_PATH, logPath);
        wcscat_s(logPath, L"Genshin.Fps.UnlockerIsland.log");
        Logger::GetInstance().Initialize(logPath);
        LOG_INFO("DLL Process Attach - Starting initialization");
        
        CreateThread(NULL, 0, Run, NULL, 0, NULL);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        LOG_INFO("DLL Process Detach - Shutting down");
        MinHookManager::DisableAllHooks();
        MH_Uninitialize();
        Logger::GetInstance().Shutdown();
    }
    return TRUE;
}