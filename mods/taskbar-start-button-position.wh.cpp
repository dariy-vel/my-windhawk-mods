// ==WindhawkMod==
// @id              taskbar-start-button-position
// @name            Start button always on the left
// @description     Forces the start button to be on the left of the taskbar, even when taskbar icons are centered (Windows 11 only)
// @version         1.2
// @author          m417z
// @github          https://github.com/m417z
// @twitter         https://twitter.com/m417z
// @homepage        https://m417z.com/
// @include         explorer.exe
// @include         StartMenuExperienceHost.exe
// @include         SearchHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshcore
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// For bug reports and feature requests, please open an issue here:
// https://github.com/ramensoftware/windhawk-mods/issues
//
// For pull requests, development takes place here:
// https://github.com/m417z/my-windhawk-mods

// ==WindhawkModReadme==
/*
# Start button always on the left

Forces the start button to be on the left of the taskbar, even when taskbar
icons are centered.

Only Windows 11 is supported.

![Screenshot](https://i.imgur.com/MSKYKbE.png)
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- startMenuOnTheLeft: true
  $name: Start menu on the left
  $description: >-
    Make the start menu open on the left even if taskbar icons are centered
- startMenuWidth: 0
  $name: Start menu width
  $description: >-
    Set to zero to use the system default width, set to a custom value if using
    a customized start menu, e.g. with the Windows 11 Start Menu Styler mod
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <functional>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

using namespace winrt::Windows::UI::Xaml;

struct {
    bool startMenuOnTheLeft;
    int startMenuWidth;
} g_settings;

enum class Target {
    Explorer,
    StartMenu,
    SearchHost,
};

Target g_target;

std::atomic<bool> g_unloading;

typedef enum MONITOR_DPI_TYPE {
    MDT_EFFECTIVE_DPI = 0,
    MDT_ANGULAR_DPI = 1,
    MDT_RAW_DPI = 2,
    MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
STDAPI GetDpiForMonitor(HMONITOR hmonitor,
                        MONITOR_DPI_TYPE dpiType,
                        UINT* dpiX,
                        UINT* dpiY);

HWND GetTaskbarWnd() {
    static HWND hTaskbarWnd;

    if (!hTaskbarWnd) {
        HWND hWnd = FindWindow(L"Shell_TrayWnd", nullptr);

        DWORD processId = 0;
        if (hWnd && GetWindowThreadProcessId(hWnd, &processId) &&
            processId == GetCurrentProcessId()) {
            hTaskbarWnd = hWnd;
        }
    }

    return hTaskbarWnd;
}

FrameworkElement EnumChildElements(
    FrameworkElement element,
    std::function<bool(FrameworkElement)> enumCallback) {
    int childrenCount = Media::VisualTreeHelper::GetChildrenCount(element);

    for (int i = 0; i < childrenCount; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (!child) {
            Wh_Log(L"Failed to get child %d of %d", i + 1, childrenCount);
            continue;
        }

        if (enumCallback(child)) {
            return child;
        }
    }

    return nullptr;
}

FrameworkElement FindChildByName(FrameworkElement element, PCWSTR name) {
    return EnumChildElements(element, [name](FrameworkElement child) {
        return child.Name() == name;
    });
}

FrameworkElement FindChildByClassName(FrameworkElement element,
                                      PCWSTR className) {
    return EnumChildElements(element, [className](FrameworkElement child) {
        return winrt::get_class_name(child) == className;
    });
}

bool ApplyStyle(XamlRoot xamlRoot) {
    FrameworkElement xamlRootContent =
        xamlRoot.Content().try_as<FrameworkElement>();

    FrameworkElement taskbarFrameRepeater = nullptr;

    FrameworkElement child = xamlRootContent;
    if (child &&
        (child = FindChildByClassName(child, L"Taskbar.TaskbarFrame")) &&
        (child = FindChildByName(child, L"RootGrid")) &&
        (child = FindChildByName(child, L"TaskbarFrameRepeater"))) {
        taskbarFrameRepeater = child;
    }

    if (!taskbarFrameRepeater) {
        return false;
    }

    auto startButton =
        EnumChildElements(taskbarFrameRepeater, [](FrameworkElement child) {
            auto childClassName = winrt::get_class_name(child);
            if (childClassName != L"Taskbar.ExperienceToggleButton") {
                return false;
            }

            auto automationId =
                Automation::AutomationProperties::GetAutomationId(child);
            return automationId == L"StartButton";
        });
    if (startButton) {
        double startButtonWidth = startButton.ActualWidth();

        Thickness startButtonMargin = startButton.Margin();
        startButtonMargin.Right = g_unloading ? 0 : -startButtonWidth;
        startButton.Margin(startButtonMargin);
    }

    auto widgetElement =
        EnumChildElements(taskbarFrameRepeater, [](FrameworkElement child) {
            auto childClassName = winrt::get_class_name(child);
            if (childClassName != L"Taskbar.AugmentedEntryPointButton") {
                return false;
            }

            if (child.Name() != L"AugmentedEntryPointButton") {
                return false;
            }

            auto margin = child.Margin();

            auto offset = child.ActualOffset();
            if (offset.x != margin.Left || offset.y != 0) {
                return false;
            }

            return true;
        });
    if (widgetElement) {
        auto margin = widgetElement.Margin();
        margin.Left = g_unloading ? 0 : 34;
        widgetElement.Margin(margin);
    }

    return true;
}

void* CTaskBand_ITaskListWndSite_vftable;

void* CSecondaryTaskBand_ITaskListWndSite_vftable;

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void* pThis, void** result);
CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original;

using CSecondaryTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void* pThis,
                                                           void** result);
CSecondaryTaskBand_GetTaskbarHost_t CSecondaryTaskBand_GetTaskbarHost_Original;

using std__Ref_count_base__Decref_t = void(WINAPI*)(void* pThis);
std__Ref_count_base__Decref_t std__Ref_count_base__Decref_Original;

XamlRoot XamlRootFromTaskbarHostSharedPtr(void* taskbarHostSharedPtr[2]) {
    if (!taskbarHostSharedPtr[0] && !taskbarHostSharedPtr[1]) {
        return nullptr;
    }

    // Reference: TaskbarHost::FrameHeight
    constexpr size_t kTaskbarElementIUnknownOffset = 0x40;

    auto* taskbarElementIUnknown =
        *(IUnknown**)((BYTE*)taskbarHostSharedPtr[0] +
                      kTaskbarElementIUnknownOffset);

    FrameworkElement taskbarElement = nullptr;
    taskbarElementIUnknown->QueryInterface(winrt::guid_of<FrameworkElement>(),
                                           winrt::put_abi(taskbarElement));

    auto result = taskbarElement ? taskbarElement.XamlRoot() : nullptr;

    std__Ref_count_base__Decref_Original(taskbarHostSharedPtr[1]);

    return result;
}

XamlRoot GetTaskbarXamlRoot(HWND hTaskbarWnd) {
    HWND hTaskSwWnd = (HWND)GetProp(hTaskbarWnd, L"TaskbandHWND");
    if (!hTaskSwWnd) {
        return nullptr;
    }

    void* taskBand = (void*)GetWindowLongPtr(hTaskSwWnd, 0);
    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite !=
                    CTaskBand_ITaskListWndSite_vftable;
         i++) {
        if (i == 20) {
            return nullptr;
        }

        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
    }

    void* taskbarHostSharedPtr[2]{};
    CTaskBand_GetTaskbarHost_Original(taskBandForTaskListWndSite,
                                      taskbarHostSharedPtr);

    return XamlRootFromTaskbarHostSharedPtr(taskbarHostSharedPtr);
}

XamlRoot GetSecondaryTaskbarXamlRoot(HWND hSecondaryTaskbarWnd) {
    HWND hTaskSwWnd =
        (HWND)FindWindowEx(hSecondaryTaskbarWnd, nullptr, L"WorkerW", nullptr);
    if (!hTaskSwWnd) {
        return nullptr;
    }

    void* taskBand = (void*)GetWindowLongPtr(hTaskSwWnd, 0);
    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite !=
                    CSecondaryTaskBand_ITaskListWndSite_vftable;
         i++) {
        if (i == 20) {
            return nullptr;
        }

        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
    }

    void* taskbarHostSharedPtr[2]{};
    CSecondaryTaskBand_GetTaskbarHost_Original(taskBandForTaskListWndSite,
                                               taskbarHostSharedPtr);

    return XamlRootFromTaskbarHostSharedPtr(taskbarHostSharedPtr);
}

using RunFromWindowThreadProc_t = void(WINAPI*)(void* parameter);

bool RunFromWindowThread(HWND hWnd,
                         RunFromWindowThreadProc_t proc,
                         void* procParam) {
    static const UINT runFromWindowThreadRegisteredMsg =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct RUN_FROM_WINDOW_THREAD_PARAM {
        RunFromWindowThreadProc_t proc;
        void* procParam;
    };

    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) {
        return false;
    }

    if (dwThreadId == GetCurrentThreadId()) {
        proc(procParam);
        return true;
    }

    HHOOK hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        [](int nCode, WPARAM wParam, LPARAM lParam) WINAPI -> LRESULT {
            if (nCode == HC_ACTION) {
                const CWPSTRUCT* cwp = (const CWPSTRUCT*)lParam;
                if (cwp->message == runFromWindowThreadRegisteredMsg) {
                    RUN_FROM_WINDOW_THREAD_PARAM* param =
                        (RUN_FROM_WINDOW_THREAD_PARAM*)cwp->lParam;
                    param->proc(param->procParam);
                }
            }

            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        },
        nullptr, dwThreadId);
    if (!hook) {
        return false;
    }

    RUN_FROM_WINDOW_THREAD_PARAM param;
    param.proc = proc;
    param.procParam = procParam;
    SendMessage(hWnd, runFromWindowThreadRegisteredMsg, 0, (LPARAM)&param);

    UnhookWindowsHookEx(hook);

    return true;
}

void ApplySettingsFromTaskbarThread() {
    Wh_Log(L"Applying settings");

    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND hWnd, LPARAM lParam) WINAPI -> BOOL {
            WCHAR szClassName[32];
            if (GetClassName(hWnd, szClassName, ARRAYSIZE(szClassName)) == 0) {
                return TRUE;
            }

            XamlRoot xamlRoot = nullptr;
            if (_wcsicmp(szClassName, L"Shell_TrayWnd") == 0) {
                xamlRoot = GetTaskbarXamlRoot(hWnd);
            } else if (_wcsicmp(szClassName, L"Shell_SecondaryTrayWnd") == 0) {
                xamlRoot = GetSecondaryTaskbarXamlRoot(hWnd);
            } else {
                return TRUE;
            }

            if (!xamlRoot) {
                Wh_Log(L"Getting XamlRoot failed");
                return TRUE;
            }

            if (!ApplyStyle(xamlRoot)) {
                Wh_Log(L"ApplyStyles failed");
                return TRUE;
            }

            return TRUE;
        },
        0);
}

void ApplySettings(HWND hTaskbarWnd) {
    RunFromWindowThread(
        hTaskbarWnd,
        [](void* pParam) WINAPI { ApplySettingsFromTaskbarThread(); }, 0);
}

using IUIElement_Arrange_t = HRESULT(
    WINAPI*)(UIElement pThis, const winrt::Windows::Foundation::Rect* rect);
IUIElement_Arrange_t IUIElement_Arrange_Original;
HRESULT WINAPI
IUIElement_Arrange_Hook(UIElement pThis,
                        const winrt::Windows::Foundation::Rect* rect) {
    Wh_Log(L">");

    auto original = [&] { return IUIElement_Arrange_Original(pThis, rect); };

    if (g_unloading) {
        return original();
    }

    FrameworkElement element = pThis.try_as<FrameworkElement>();
    if (!element) {
        return original();
    }

    auto className = winrt::get_class_name(element);
    if (className != L"Taskbar.ExperienceToggleButton") {
        return original();
    }

    auto automationId =
        Automation::AutomationProperties::GetAutomationId(element);
    if (automationId != L"StartButton") {
        return original();
    }

    auto taskbarFrameRepeater =
        Media::VisualTreeHelper::GetParent(element).as<FrameworkElement>();
    auto widgetElement =
        EnumChildElements(taskbarFrameRepeater, [](FrameworkElement child) {
            auto childClassName = winrt::get_class_name(child);
            if (childClassName != L"Taskbar.AugmentedEntryPointButton") {
                return false;
            }

            if (child.Name() != L"AugmentedEntryPointButton") {
                return false;
            }

            auto margin = child.Margin();

            auto offset = child.ActualOffset();
            if (offset.x != margin.Left || offset.y != 0) {
                return false;
            }

            return true;
        });

    if (!widgetElement) {
        element.Dispatcher().TryRunAsync(
            winrt::Windows::UI::Core::CoreDispatcherPriority::High,
            [element]() {
                double width = element.ActualWidth();

                double minX = std::numeric_limits<double>::infinity();
                auto taskbarFrameRepeater =
                    Media::VisualTreeHelper::GetParent(element)
                        .as<FrameworkElement>();
                EnumChildElements(taskbarFrameRepeater,
                                  [&element, &minX](FrameworkElement child) {
                                      if (child == element) {
                                          return false;
                                      }

                                      auto offset = child.ActualOffset();
                                      if (offset.x >= 0 && offset.x < minX) {
                                          minX = offset.x;
                                      }

                                      return false;
                                  });

                if (minX < width) {
                    Thickness margin = element.Margin();
                    margin.Right = 0;
                    element.Margin(margin);
                } else if (minX > width * 2) {
                    Thickness margin = element.Margin();
                    margin.Right = -width;
                    element.Margin(margin);
                }
            });
    }

    // Force the start button to have X = 0.
    winrt::Windows::Foundation::Rect newRect = *rect;
    newRect.X = 0;
    return IUIElement_Arrange_Original(pThis, &newRect);
}

using AugmentedEntryPointButton_UpdateButtonPadding_t =
    void(WINAPI*)(void* pThis);
AugmentedEntryPointButton_UpdateButtonPadding_t
    AugmentedEntryPointButton_UpdateButtonPadding_Original;
void WINAPI AugmentedEntryPointButton_UpdateButtonPadding_Hook(void* pThis) {
    Wh_Log(L">");

    AugmentedEntryPointButton_UpdateButtonPadding_Original(pThis);

    if (g_unloading) {
        return;
    }

    FrameworkElement button = nullptr;
    ((IUnknown**)pThis)[1]->QueryInterface(winrt::guid_of<FrameworkElement>(),
                                           winrt::put_abi(button));
    if (!button) {
        return;
    }

    button.Dispatcher().TryRunAsync(
        winrt::Windows::UI::Core::CoreDispatcherPriority::High, [button]() {
            auto offset = button.ActualOffset();
            if (offset.x != 0 || offset.y != 0) {
                return;
            }

            auto margin = button.Margin();
            margin.Left = 34;
            button.Margin(margin);
        });
}

bool HookTaskbarDllSymbols() {
    HMODULE module = LoadLibrary(L"taskbar.dll");
    if (!module) {
        Wh_Log(L"Failed to load taskbar.dll");
        return false;
    }

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {
            {LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"},
            &CTaskBand_ITaskListWndSite_vftable,
        },
        {
            {LR"(const CSecondaryTaskBand::`vftable'{for `ITaskListWndSite'})"},
            &CSecondaryTaskBand_ITaskListWndSite_vftable,
        },
        {
            {LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"},
            &CTaskBand_GetTaskbarHost_Original,
        },
        {
            {LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CSecondaryTaskBand::GetTaskbarHost(void)const )"},
            &CSecondaryTaskBand_GetTaskbarHost_Original,
        },
        {
            {LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"},
            &std__Ref_count_base__Decref_Original,
        },
    };

    return HookSymbols(module, taskbarDllHooks, ARRAYSIZE(taskbarDllHooks));
}

bool HookTaskbarViewDllSymbols() {
    WCHAR dllPath[MAX_PATH];
    if (!GetWindowsDirectory(dllPath, ARRAYSIZE(dllPath))) {
        Wh_Log(L"GetWindowsDirectory failed");
        return false;
    }

    wcscat_s(
        dllPath, MAX_PATH,
        LR"(\SystemApps\MicrosoftWindows.Client.Core_cw5n1h2txyewy\Taskbar.View.dll)");

    HMODULE module =
        LoadLibraryEx(dllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        Wh_Log(L"Taskbar view module couldn't be loaded");
        return false;
    }

    // Taskbar.View.dll
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(public: __cdecl winrt::impl::consume_Windows_UI_Xaml_IUIElement<struct winrt::Windows::UI::Xaml::IUIElement>::Arrange(struct winrt::Windows::Foundation::Rect const &)const )"},
            &IUIElement_Arrange_Original,
            IUIElement_Arrange_Hook,
        },
        {
            {LR"(protected: virtual void __cdecl winrt::Taskbar::implementation::AugmentedEntryPointButton::UpdateButtonPadding(void))"},
            &AugmentedEntryPointButton_UpdateButtonPadding_Original,
            AugmentedEntryPointButton_UpdateButtonPadding_Hook,
        },
    };

    return HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks));
}

namespace CoreWindowUI {

using SetWindowPos_t = decltype(&SetWindowPos);
SetWindowPos_t SetWindowPos_Original;

bool IsTargetCoreWindow(HWND hWnd) {
    DWORD threadId = 0;
    DWORD processId = 0;
    if (!hWnd || !(threadId = GetWindowThreadProcessId(hWnd, &processId)) ||
        processId != GetCurrentProcessId()) {
        return false;
    }

    WCHAR szClassName[32];
    if (GetClassName(hWnd, szClassName, ARRAYSIZE(szClassName)) == 0) {
        return false;
    }

    if (_wcsicmp(szClassName, L"Windows.UI.Core.CoreWindow") != 0) {
        return false;
    }

    return true;
}

std::vector<HWND> GetCoreWindows() {
    struct ENUM_WINDOWS_PARAM {
        std::vector<HWND>* hWnds;
    };

    std::vector<HWND> hWnds;
    ENUM_WINDOWS_PARAM param = {&hWnds};
    EnumWindows(
        [](HWND hWnd, LPARAM lParam) WINAPI -> BOOL {
            ENUM_WINDOWS_PARAM& param = *(ENUM_WINDOWS_PARAM*)lParam;

            if (IsTargetCoreWindow(hWnd)) {
                param.hWnds->push_back(hWnd);
            }

            return TRUE;
        },
        (LPARAM)&param);

    return hWnds;
}

void AdjustCoreWindowSize(int x, int y, int* width, int* height) {
    if (g_target != Target::StartMenu) {
        return;
    }

    const POINT pt = {x, y};
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    if (g_unloading) {
        MONITORINFO monitorInfo{
            .cbSize = sizeof(MONITORINFO),
        };
        GetMonitorInfo(monitor, &monitorInfo);

        *width = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        // *height = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        return;
    }

    UINT monitorDpiX = 96;
    UINT monitorDpiY = 96;
    GetDpiForMonitor(monitor, MDT_DEFAULT, &monitorDpiX, &monitorDpiY);

    const int w1 =
        MulDiv(g_settings.startMenuWidth ? g_settings.startMenuWidth : 660,
               monitorDpiX, 96);
    if (*width > w1) {
        *width = w1;
    }

    // const int h1 = MulDiv(750, monitorDpiY, 96);
    // const int h2 = MulDiv(694, monitorDpiY, 96);
    // if (*height >= h1) {
    //     *height = h1;
    // } else if (*height >= h2) {
    //     *height = h2;
    // }
}

void AdjustCoreWindowPos(int* x, int* y, int width, int height) {
    if (g_unloading) {
        if (g_target == Target::StartMenu) {
            *x = 0;
            *y = 0;
        }

        return;
    }

    const POINT pt = {*x, *y};
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    MONITORINFO monitorInfo{
        .cbSize = sizeof(MONITORINFO),
    };
    GetMonitorInfo(monitor, &monitorInfo);

    *x = monitorInfo.rcWork.left;
}

void ApplySettings() {
    for (HWND hCoreWnd : GetCoreWindows()) {
        Wh_Log(L"Adjusting core window %08X", (DWORD)(ULONG_PTR)hCoreWnd);

        RECT rc;
        if (!GetWindowRect(hCoreWnd, &rc)) {
            continue;
        }

        int x = rc.left;
        int y = rc.top;
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;

        AdjustCoreWindowSize(x, y, &cx, &cy);
        AdjustCoreWindowPos(&x, &y, cx, cy);

        SetWindowPos_Original(hCoreWnd, nullptr, x, y, cx, cy,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

using CreateWindowInBand_t = HWND(WINAPI*)(DWORD dwExStyle,
                                           LPCWSTR lpClassName,
                                           LPCWSTR lpWindowName,
                                           DWORD dwStyle,
                                           int X,
                                           int Y,
                                           int nWidth,
                                           int nHeight,
                                           HWND hWndParent,
                                           HMENU hMenu,
                                           HINSTANCE hInstance,
                                           PVOID lpParam,
                                           DWORD dwBand);
CreateWindowInBand_t CreateWindowInBand_Original;
HWND WINAPI CreateWindowInBand_Hook(DWORD dwExStyle,
                                    LPCWSTR lpClassName,
                                    LPCWSTR lpWindowName,
                                    DWORD dwStyle,
                                    int X,
                                    int Y,
                                    int nWidth,
                                    int nHeight,
                                    HWND hWndParent,
                                    HMENU hMenu,
                                    HINSTANCE hInstance,
                                    PVOID lpParam,
                                    DWORD dwBand) {
    BOOL bTextualClassName = ((ULONG_PTR)lpClassName & ~(ULONG_PTR)0xffff) != 0;
    if (bTextualClassName &&
        _wcsicmp(lpClassName, L"Windows.UI.Core.CoreWindow") == 0) {
        Wh_Log(L"Creating core window");
        AdjustCoreWindowSize(X, Y, &nWidth, &nHeight);
        AdjustCoreWindowPos(&X, &Y, nWidth, nHeight);
    }

    return CreateWindowInBand_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand);
}

using CreateWindowInBandEx_t = HWND(WINAPI*)(DWORD dwExStyle,
                                             LPCWSTR lpClassName,
                                             LPCWSTR lpWindowName,
                                             DWORD dwStyle,
                                             int X,
                                             int Y,
                                             int nWidth,
                                             int nHeight,
                                             HWND hWndParent,
                                             HMENU hMenu,
                                             HINSTANCE hInstance,
                                             PVOID lpParam,
                                             DWORD dwBand,
                                             DWORD dwTypeFlags);
CreateWindowInBandEx_t CreateWindowInBandEx_Original;
HWND WINAPI CreateWindowInBandEx_Hook(DWORD dwExStyle,
                                      LPCWSTR lpClassName,
                                      LPCWSTR lpWindowName,
                                      DWORD dwStyle,
                                      int X,
                                      int Y,
                                      int nWidth,
                                      int nHeight,
                                      HWND hWndParent,
                                      HMENU hMenu,
                                      HINSTANCE hInstance,
                                      PVOID lpParam,
                                      DWORD dwBand,
                                      DWORD dwTypeFlags) {
    BOOL bTextualClassName = ((ULONG_PTR)lpClassName & ~(ULONG_PTR)0xffff) != 0;
    if (bTextualClassName &&
        _wcsicmp(lpClassName, L"Windows.UI.Core.CoreWindow") == 0) {
        Wh_Log(L"Creating core window");
        AdjustCoreWindowSize(X, Y, &nWidth, &nHeight);
        AdjustCoreWindowPos(&X, &Y, nWidth, nHeight);
    }

    return CreateWindowInBandEx_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand, dwTypeFlags);
}

BOOL WINAPI SetWindowPos_Hook(HWND hWnd,
                              HWND hWndInsertAfter,
                              int X,
                              int Y,
                              int cx,
                              int cy,
                              UINT uFlags) {
    auto original = [&]() {
        return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy,
                                     uFlags);
    };

    if (!IsTargetCoreWindow(hWnd)) {
        return original();
    }

    Wh_Log(L"%08X %08X", (DWORD)(ULONG_PTR)hWnd, uFlags);

    if ((uFlags & (SWP_NOSIZE | SWP_NOMOVE)) == (SWP_NOSIZE | SWP_NOMOVE)) {
        return original();
    }

    RECT rc{};
    GetWindowRect(hWnd, &rc);

    // SearchHost is being moved by explorer.exe, then the size is adjusted
    // by SearchHost itself. Make SearchHost adjust the position too. A
    // similar workaround is needed for other windows.
    if (uFlags & SWP_NOMOVE) {
        uFlags &= ~SWP_NOMOVE;
        X = rc.left;
        Y = rc.top;
    }

    int width;
    int height;
    if (uFlags & SWP_NOSIZE) {
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
        AdjustCoreWindowSize(X, Y, &width, &height);
    } else {
        AdjustCoreWindowSize(X, Y, &cx, &cy);
        width = cx;
        height = cy;
    }

    if (!(uFlags & SWP_NOMOVE)) {
        AdjustCoreWindowPos(&X, &Y, width, height);
    }

    return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

}  // namespace CoreWindowUI

void LoadSettings() {
    g_settings.startMenuOnTheLeft = Wh_GetIntSetting(L"startMenuOnTheLeft");
    g_settings.startMenuWidth = Wh_GetIntSetting(L"startMenuWidth");
}

BOOL Wh_ModInit() {
    Wh_Log(L">");

    LoadSettings();

    g_target = Target::Explorer;

    WCHAR moduleFilePath[MAX_PATH];
    switch (
        GetModuleFileName(nullptr, moduleFilePath, ARRAYSIZE(moduleFilePath))) {
        case 0:
        case ARRAYSIZE(moduleFilePath):
            Wh_Log(L"GetModuleFileName failed");
            break;

        default:
            if (PCWSTR moduleFileName = wcsrchr(moduleFilePath, L'\\')) {
                moduleFileName++;
                if (_wcsicmp(moduleFileName, L"StartMenuExperienceHost.exe") ==
                    0) {
                    g_target = Target::StartMenu;
                } else if (_wcsicmp(moduleFileName, L"SearchHost.exe") == 0) {
                    g_target = Target::SearchHost;
                }
            } else {
                Wh_Log(L"GetModuleFileName returned an unsupported path");
            }
            break;
    }

    if (g_target == Target::StartMenu || g_target == Target::SearchHost) {
        if (!g_settings.startMenuOnTheLeft) {
            return FALSE;
        }

        HMODULE user32Module = LoadLibrary(L"user32.dll");
        if (user32Module) {
            void* pCreateWindowInBand =
                (void*)GetProcAddress(user32Module, "CreateWindowInBand");
            if (pCreateWindowInBand) {
                Wh_SetFunctionHook(
                    pCreateWindowInBand,
                    (void*)CoreWindowUI::CreateWindowInBand_Hook,
                    (void**)&CoreWindowUI::CreateWindowInBand_Original);
            }

            void* pCreateWindowInBandEx =
                (void*)GetProcAddress(user32Module, "CreateWindowInBandEx");
            if (pCreateWindowInBandEx) {
                Wh_SetFunctionHook(
                    pCreateWindowInBandEx,
                    (void*)CoreWindowUI::CreateWindowInBandEx_Hook,
                    (void**)&CoreWindowUI::CreateWindowInBandEx_Original);
            }
        }

        Wh_SetFunctionHook((void*)SetWindowPos,
                           (void*)CoreWindowUI::SetWindowPos_Hook,
                           (void**)&CoreWindowUI::SetWindowPos_Original);
        return TRUE;
    }

    if (!HookTaskbarDllSymbols()) {
        return FALSE;
    }

    if (!HookTaskbarViewDllSymbols()) {
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    if (g_target == Target::Explorer) {
        HWND hTaskbarWnd = GetTaskbarWnd();
        if (hTaskbarWnd) {
            ApplySettings(hTaskbarWnd);
        }
    } else if (g_target == Target::StartMenu ||
               g_target == Target::SearchHost) {
        CoreWindowUI::ApplySettings();
    }
}

void Wh_ModBeforeUninit() {
    Wh_Log(L">");

    g_unloading = true;

    if (g_target == Target::Explorer) {
        HWND hTaskbarWnd = GetTaskbarWnd();
        if (hTaskbarWnd) {
            ApplySettings(hTaskbarWnd);
        }
    } else if (g_target == Target::StartMenu ||
               g_target == Target::SearchHost) {
        CoreWindowUI::ApplySettings();
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    Wh_Log(L">");

    LoadSettings();

    if (g_target == Target::StartMenu || g_target == Target::SearchHost) {
        if (!g_settings.startMenuOnTheLeft) {
            return FALSE;
        }

        CoreWindowUI::ApplySettings();
    }

    return TRUE;
}
