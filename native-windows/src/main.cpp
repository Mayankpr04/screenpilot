#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../resources/resource.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"ScreenPilotNativeWindow";
constexpr wchar_t kWindowTitle[] = L"ScreenPilot";
constexpr wchar_t kAppId[] = L"MayankPratap.ScreenPilot.2";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kMenuOpen = 1001;
constexpr UINT kMenuRefresh = 1002;
constexpr UINT kMenuExit = 1003;

enum class ControlKind { Brightness, Contrast, BlackLevel };

struct RangeValue {
    DWORD minimum{};
    DWORD current{};
    DWORD maximum{100};
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual std::optional<RangeValue> Read(ControlKind kind) = 0;
    virtual bool Write(ControlKind kind, DWORD value) = 0;
};

class DdcBackend final : public Backend {
public:
    explicit DdcBackend(HANDLE handle) : handle_(handle) {}
    ~DdcBackend() override {
        if (handle_) DestroyPhysicalMonitor(handle_);
    }
    DdcBackend(const DdcBackend&) = delete;
    DdcBackend& operator=(const DdcBackend&) = delete;

    std::optional<RangeValue> Read(ControlKind kind) override {
        DWORD low = 0, current = 0, high = 0;
        BOOL ok = FALSE;
        switch (kind) {
        case ControlKind::Brightness:
            ok = GetMonitorBrightness(handle_, &low, &current, &high);
            break;
        case ControlKind::Contrast:
            ok = GetMonitorContrast(handle_, &low, &current, &high);
            break;
        case ControlKind::BlackLevel: {
            MC_VCP_CODE_TYPE type{};
            ok = GetVCPFeatureAndVCPFeatureReply(handle_, 0x11, &type, &current, &high);
            low = 0;
            break;
        }
        }
        if (!ok || high <= low) return std::nullopt;
        return RangeValue{low, current, high};
    }

    bool Write(ControlKind kind, DWORD value) override {
        switch (kind) {
        case ControlKind::Brightness: return SetMonitorBrightness(handle_, value) != FALSE;
        case ControlKind::Contrast: return SetMonitorContrast(handle_, value) != FALSE;
        case ControlKind::BlackLevel: return SetVCPFeature(handle_, 0x11, value) != FALSE;
        }
        return false;
    }

private:
    HANDLE handle_{};
};

std::wstring EscapeWql(std::wstring value) {
    size_t offset = 0;
    while ((offset = value.find(L'\\', offset)) != std::wstring::npos) {
        value.insert(offset, 1, L'\\');
        offset += 2;
    }
    offset = 0;
    while ((offset = value.find(L'\'', offset)) != std::wstring::npos) {
        value.insert(offset, 1, L'\\');
        offset += 2;
    }
    return value;
}

class WmiConnection {
public:
    bool Connect() {
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&locator_));
        if (FAILED(hr)) return false;
        hr = locator_->ConnectServer(
            const_cast<BSTR>(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_);
        if (FAILED(hr)) return false;
        hr = CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                               EOAC_NONE);
        return SUCCEEDED(hr);
    }

    IWbemServices* Services() const { return services_.Get(); }

private:
    ComPtr<IWbemLocator> locator_;
    ComPtr<IWbemServices> services_;
};

class WmiBacklightBackend final : public Backend {
public:
    WmiBacklightBackend(std::shared_ptr<WmiConnection> connection, std::wstring instance,
                        std::wstring methodPath)
        : connection_(std::move(connection)), instance_(std::move(instance)),
          methodPath_(std::move(methodPath)) {}

    std::optional<RangeValue> Read(ControlKind kind) override {
        if (kind != ControlKind::Brightness) return std::nullopt;
        std::wstring query = L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE InstanceName='" +
                             EscapeWql(instance_) + L"'";
        ComPtr<IEnumWbemClassObject> enumerator;
        HRESULT hr = connection_->Services()->ExecQuery(
            const_cast<BSTR>(L"WQL"), const_cast<BSTR>(query.c_str()),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
        if (FAILED(hr)) return std::nullopt;
        ComPtr<IWbemClassObject> item;
        ULONG returned = 0;
        if (FAILED(enumerator->Next(1500, 1, &item, &returned)) || returned == 0) return std::nullopt;
        VARIANT value;
        VariantInit(&value);
        hr = item->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr);
        if (FAILED(hr)) return std::nullopt;
        DWORD current = value.vt == VT_UI1 ? value.bVal : value.uintVal;
        VariantClear(&value);
        return RangeValue{0, current, 100};
    }

    bool Write(ControlKind kind, DWORD value) override {
        if (kind != ControlKind::Brightness) return false;
        ComPtr<IWbemClassObject> methodClass;
        ComPtr<IWbemClassObject> inputDefinition;
        ComPtr<IWbemClassObject> input;
        HRESULT hr = connection_->Services()->GetObject(
            const_cast<BSTR>(L"WmiMonitorBrightnessMethods"), 0, nullptr, &methodClass, nullptr);
        if (FAILED(hr)) return false;
        hr = methodClass->GetMethod(L"WmiSetBrightness", 0, &inputDefinition, nullptr);
        if (FAILED(hr)) return false;
        hr = inputDefinition->SpawnInstance(0, &input);
        if (FAILED(hr)) return false;

        VARIANT timeout;
        VariantInit(&timeout);
        timeout.vt = VT_UI4;
        timeout.ulVal = 0;
        input->Put(L"Timeout", 0, &timeout, 0);
        VARIANT brightness;
        VariantInit(&brightness);
        brightness.vt = VT_UI1;
        brightness.bVal = static_cast<BYTE>(std::min<DWORD>(100, value));
        input->Put(L"Brightness", 0, &brightness, 0);
        hr = connection_->Services()->ExecMethod(
            const_cast<BSTR>(methodPath_.c_str()), const_cast<BSTR>(L"WmiSetBrightness"),
            0, nullptr, input.Get(), nullptr, nullptr);
        return SUCCEEDED(hr);
    }

private:
    std::shared_ptr<WmiConnection> connection_;
    std::wstring instance_;
    std::wstring methodPath_;
};

struct Display {
    std::wstring name;
    std::wstring type;
    std::unique_ptr<Backend> backend;
    std::unordered_map<ControlKind, RangeValue> controls;
};

void Probe(Display& display) {
    display.controls.clear();
    for (ControlKind kind : {ControlKind::Brightness, ControlKind::Contrast, ControlKind::BlackLevel}) {
        if (auto value = display.backend->Read(kind)) display.controls.emplace(kind, *value);
    }
}

BOOL CALLBACK CollectDdc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* displays = reinterpret_cast<std::vector<Display>*>(data);
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0) return TRUE;
    std::vector<PHYSICAL_MONITOR> physical(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data())) return TRUE;
    for (auto& item : physical) {
        Display display;
        display.name = item.szPhysicalMonitorDescription[0]
                           ? item.szPhysicalMonitorDescription
                           : L"External monitor";
        display.type = L"External / DDC-CI";
        display.backend = std::make_unique<DdcBackend>(item.hPhysicalMonitor);
        item.hPhysicalMonitor = nullptr;
        Probe(display);
        if (!display.controls.empty()) displays->push_back(std::move(display));
    }
    for (auto& item : physical) {
        if (item.hPhysicalMonitor) DestroyPhysicalMonitor(item.hPhysicalMonitor);
    }
    return TRUE;
}

std::vector<Display> DiscoverWmiBacklights() {
    std::vector<Display> displays;
    auto connection = std::make_shared<WmiConnection>();
    if (!connection->Connect()) return displays;

    ComPtr<IEnumWbemClassObject> enumerator;
    HRESULT hr = connection->Services()->ExecQuery(
        const_cast<BSTR>(L"WQL"),
        const_cast<BSTR>(L"SELECT InstanceName, __PATH FROM WmiMonitorBrightnessMethods"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr)) return displays;

    while (true) {
        ComPtr<IWbemClassObject> item;
        ULONG returned = 0;
        if (FAILED(enumerator->Next(1000, 1, &item, &returned)) || returned == 0) break;
        VARIANT instance, path;
        VariantInit(&instance);
        VariantInit(&path);
        if (SUCCEEDED(item->Get(L"InstanceName", 0, &instance, nullptr, nullptr)) &&
            SUCCEEDED(item->Get(L"__PATH", 0, &path, nullptr, nullptr)) &&
            instance.vt == VT_BSTR && path.vt == VT_BSTR) {
            std::wstring instanceName(instance.bstrVal);
            std::wstring label = L"Built-in display";
            auto slash = instanceName.find(L'\\');
            auto second = slash == std::wstring::npos ? slash : instanceName.find(L'\\', slash + 1);
            if (slash != std::wstring::npos && second != std::wstring::npos)
                label += L" (" + instanceName.substr(slash + 1, second - slash - 1) + L")";
            Display display{label, L"Laptop panel",
                            std::make_unique<WmiBacklightBackend>(connection, instanceName, path.bstrVal), {}};
            Probe(display);
            if (!display.controls.empty()) displays.push_back(std::move(display));
        }
        VariantClear(&instance);
        VariantClear(&path);
    }
    return displays;
}

std::vector<Display> DiscoverDisplays() {
    auto displays = DiscoverWmiBacklights();
    EnumDisplayMonitors(nullptr, nullptr, CollectDdc, reinterpret_cast<LPARAM>(&displays));
    return displays;
}

const wchar_t* ControlLabel(ControlKind kind) {
    switch (kind) {
    case ControlKind::Brightness: return L"Brightness";
    case ControlKind::Contrast: return L"Contrast";
    case ControlKind::BlackLevel: return L"Black level";
    }
    return L"Control";
}

struct SliderBinding {
    size_t displayIndex{};
    ControlKind kind{};
    HWND valueLabel{};
};

class Application {
public:
    explicit Application(HINSTANCE instance) : instance_(instance) {}
    ~Application() {
        for (HFONT font : fonts_) DeleteObject(font);
        if (background_) DeleteObject(background_);
    }

    bool Initialize(int showCommand) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        SetCurrentProcessExplicitAppUserModelID(kAppId);

        WNDCLASSEX wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.hIcon = LoadIcon(instance_, MAKEINTRESOURCE(IDI_SCREENPILOT));
        wc.hIconSm = wc.hIcon;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        background_ = CreateSolidBrush(RGB(18, 23, 34));
        wc.hbrBackground = background_;
        wc.lpszClassName = kWindowClass;
        if (!RegisterClassEx(&wc)) return false;

        window_ = CreateWindowEx(0, kWindowClass, kWindowTitle,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 700, 620, nullptr, nullptr,
                                 instance_, this);
        if (!window_) return false;
        EnableDarkTitleBar();
        AddTrayIcon();
        Refresh();
        if (showCommand != SW_HIDE) ShowWindow(window_, showCommand);
        return true;
    }

    int Run() {
        MSG message;
        while (GetMessage(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        Application* self = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<Application*>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Application*>(GetWindowLongPtr(window, GWLP_USERDATA));
        }
        return self ? self->HandleMessage(message, wParam, lParam)
                    : DefWindowProc(window, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CLOSE:
            ShowWindow(window_, SW_HIDE);
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuOpen) Open();
            else if (LOWORD(wParam) == kMenuRefresh) Refresh();
            else if (LOWORD(wParam) == kMenuExit) DestroyWindow(window_);
            return 0;
        case WM_HSCROLL:
            OnSlider(reinterpret_cast<HWND>(lParam), LOWORD(wParam));
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(220, 228, 242));
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(background_);
        }
        case kTrayMessage:
            if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) Open();
            else if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
                ShowTrayMenu();
            return 0;
        }
        return DefWindowProc(window_, message, wParam, lParam);
    }

    void EnableDarkTitleBar() {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    }

    void AddTrayIcon() {
        tray_ = {};
        tray_.cbSize = sizeof(tray_);
        tray_.hWnd = window_;
        tray_.uID = kTrayId;
        tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        tray_.uCallbackMessage = kTrayMessage;
        tray_.hIcon = LoadIcon(instance_, MAKEINTRESOURCE(IDI_SCREENPILOT));
        wcscpy_s(tray_.szTip, L"ScreenPilot");
        Shell_NotifyIcon(NIM_ADD, &tray_);
        tray_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon(NIM_SETVERSION, &tray_);
    }

    void RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &tray_); }

    void ShowTrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenu(menu, MF_STRING | MF_DEFAULT, kMenuOpen, L"Open ScreenPilot");
        AppendMenu(menu, MF_STRING, kMenuRefresh, L"Refresh displays");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING, kMenuExit, L"Exit");
        POINT point;
        GetCursorPos(&point);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                       point.x, point.y, 0, window_, nullptr);
        DestroyMenu(menu);
    }

    void Open() {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
    }

    HWND MakeText(const std::wstring& text, int x, int y, int width, int height, int size,
                  bool bold = false) {
        HWND label = CreateWindow(L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                  x, y, width, height, window_, nullptr, instance_, nullptr);
        HFONT font = CreateFont(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE,
                                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessage(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        fonts_.push_back(font);
        return label;
    }

    void ClearControls() {
        EnumChildWindows(window_, [](HWND child, LPARAM) -> BOOL { DestroyWindow(child); return TRUE; }, 0);
        for (HFONT font : fonts_) DeleteObject(font);
        fonts_.clear();
        sliders_.clear();
    }

    void Refresh() {
        ClearControls();
        displays_ = DiscoverDisplays();
        MakeText(L"ScreenPilot", 28, 24, 320, 42, 30, true);
        HWND refresh = CreateWindow(L"BUTTON", L"Refresh displays", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    520, 26, 140, 34, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMenuRefresh)),
                                    instance_, nullptr);
        SendMessage(refresh, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        int y = 86;
        if (displays_.empty()) {
            MakeText(L"No controllable displays were detected.", 32, y, 600, 30, 18);
            return;
        }
        for (size_t index = 0; index < displays_.size(); ++index) {
            auto& display = displays_[index];
            MakeText(display.name, 34, y, 570, 28, 20, true);
            MakeText(display.type, 34, y + 30, 570, 24, 15);
            y += 63;
            for (ControlKind kind : {ControlKind::Brightness, ControlKind::Contrast, ControlKind::BlackLevel}) {
                auto found = display.controls.find(kind);
                if (found == display.controls.end()) continue;
                MakeText(ControlLabel(kind), 42, y + 4, 95, 24, 15);
                HWND slider = CreateWindowEx(0, TRACKBAR_CLASS, nullptr,
                                              WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                              140, y, 450, 30, window_, nullptr, instance_, nullptr);
                SendMessage(slider, TBM_SETRANGEMIN, FALSE, found->second.minimum);
                SendMessage(slider, TBM_SETRANGEMAX, FALSE, found->second.maximum);
                SendMessage(slider, TBM_SETPOS, TRUE, found->second.current);
                HWND value = MakeText(std::to_wstring(found->second.current), 600, y + 4, 50, 24, 15);
                sliders_.emplace(slider, SliderBinding{index, kind, value});
                y += 37;
            }
            y += 18;
        }
        RECT rect;
        GetWindowRect(window_, &rect);
        SetWindowPos(window_, nullptr, 0, 0, 700, std::clamp(y + 55, 300, 850),
                     SWP_NOMOVE | SWP_NOZORDER);
    }

    void OnSlider(HWND slider, UINT event) {
        auto found = sliders_.find(slider);
        if (found == sliders_.end()) return;
        DWORD value = static_cast<DWORD>(SendMessage(slider, TBM_GETPOS, 0, 0));
        SetWindowText(found->second.valueLabel, std::to_wstring(value).c_str());
        if (event == TB_ENDTRACK || event == TB_THUMBPOSITION) {
            auto& binding = found->second;
            if (!displays_[binding.displayIndex].backend->Write(binding.kind, value)) {
                MessageBox(window_, L"The monitor rejected this setting.", L"ScreenPilot",
                           MB_OK | MB_ICONWARNING);
            }
        }
    }

    HINSTANCE instance_{};
    HWND window_{};
    NOTIFYICONDATA tray_{};
    std::vector<Display> displays_;
    std::unordered_map<HWND, SliderBinding> sliders_;
    std::vector<HFONT> fonts_;
    HBRUSH background_{};
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    HANDLE mutex = CreateMutex(nullptr, TRUE, L"Local\\ScreenPilotNativeSingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindow(kWindowClass, nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(com)) {
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                             RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    }
    bool trayOnly = commandLine && wcsstr(commandLine, L"--tray");
    Application application(instance);
    int result = application.Initialize(trayOnly ? SW_HIDE : showCommand) ? application.Run() : 1;
    if (SUCCEEDED(com)) CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
