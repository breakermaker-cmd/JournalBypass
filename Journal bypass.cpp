#include <windows.h>
#include <tlhelp32.h>
#include <tchar.h>
#include <string>
#include <random>
#include <iostream>
#include <vector>

void EnableSystemTimePrivilege() {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp = { 0 };
        tp.PrivilegeCount = 1;
        if (LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &tp.Privileges[0].Luid)) {
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
                std::wcerr << L"AdjustTokenPrivileges failed: " << GetLastError() << std::endl;
            }
        }
        else {
            std::wcerr << L"LookupPrivilegeValue failed: " << GetLastError() << std::endl;
        }
        CloseHandle(hToken);
    }
    else {
        std::wcerr << L"OpenProcessToken failed: " << GetLastError() << std::endl;
    }
}

std::wstring GenerateRandomFileName() {
    const std::wstring chars = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, static_cast<int>(chars.length()) - 1);

    std::wstring filename;
    filename.reserve(8);
    for (int i = 0; i < 8; ++i) {
        filename += chars[dis(gen)];
    }
    return filename + L".tmp";
}

bool ClearEventLogs() {
    
    std::vector<std::string> channels = {
        "Application",
        "Security",
        "System",
        "Setup"
    };

    bool success = true;
    for (const auto& channel : channels) {
        std::string command = "wevtutil cl " + channel;
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            std::wcerr << L"Failed to clear event log " << std::wstring(channel.begin(), channel.end()) << L": " << GetLastError() << std::endl;
            success = false;
            continue;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            std::wcerr << L"wevtutil command failed for " << std::wstring(channel.begin(), channel.end()) << L" with exit code: " << exitCode << std::endl;
            success = false;
        }
    }
    return success;
}

bool CleanUsnJournalAndEventLogs() {
    
    EnableSystemTimePrivilege();

    
    SYSTEMTIME originalTime;
    GetSystemTime(&originalTime);

   
    SYSTEMTIME st;
    GetSystemTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uli.QuadPart -= (8ULL * 24 * 60 * 60 + 12ULL * 60 * 60) * 10000000;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime(&ft, &st);

    
    if (!SetSystemTime(&st)) {
        std::wcerr << L"SetSystemTime failed: " << GetLastError() << std::endl;
        return false;
    }

    
    std::string command = "fsutil usn deletejournal /d /n c:";
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::wcerr << L"CreateProcess failed: " << GetLastError() << std::endl;
        SetSystemTime(&originalTime);
        return false;
    }

    /
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        std::wcerr << L"fsutil command failed with exit code: " << exitCode << std::endl;
        SetSystemTime(&originalTime);
        return false;
    }

    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        std::wcerr << L"GetTempPath failed: " << GetLastError() << std::endl;
        SetSystemTime(&originalTime);
        return false;
    }

    std::wstring filePath = std::wstring(tempPath) + GenerateRandomFileName();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        const char* dummyData = "Dummy data";
        WriteFile(hFile, dummyData, static_cast<DWORD>(strlen(dummyData)), &bytesWritten, NULL);

        FILETIME creationTime, lastWriteTime, lastAccessTime;
        GetFileTime(hFile, &creationTime, &lastAccessTime, &lastWriteTime);
        SYSTEMTIME writeTime;
        FileTimeToSystemTime(&lastWriteTime, &writeTime);

        std::wcout << L"Dummy file write time: "
            << writeTime.wYear << L"-"
            << writeTime.wMonth << L"-"
            << writeTime.wDay << L" "
            << writeTime.wHour << L":"
            << writeTime.wMinute << L":"
            << writeTime.wSecond << std::endl;

        CloseHandle(hFile);
        if (!DeleteFileW(filePath.c_str())) {
            std::wcerr << L"DeleteFile failed: " << GetLastError() << std::endl;
        }
    }
    else {
        std::wcerr << L"CreateFile failed: " << GetLastError() << std::endl;
    }

    
    Sleep(1000);

   
    if (!SetSystemTime(&originalTime)) {
        std::wcerr << L"Restore system time failed: " << GetLastError() << std::endl;
        return false;
    }

   
    if (!ClearEventLogs()) {
        std::wcerr << L"Event log clearing failed for one or more logs" << std::endl;
        return false;
    }

    return true;
}

int wmain() {
    if (CleanUsnJournalAndEventLogs()) {
        std::wcout << L"USN Journal and Event Viewer cleaning completed successfully" << std::endl;
        return 0;
    }
    else {
        std::wcout << L"USN Journal and Event Viewer cleaning failed" << std::endl;
        return 1;
    }
}
