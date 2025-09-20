#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <tchar.h>
#include <map>
#include <set>
#include <psapi.h>
#include <string>
#include <vector>
#include <random>
#include <iostream>






void EnableSystemTimePrivilege() {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &tp.Privileges[0].Luid);
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        CloseHandle(hToken);
    }
}

std::wstring GenerateRandomFileName() {
    const std::wstring chars = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.length() - 1);

    std::wstring filename;
    for (int i = 0; i < 8; ++i) {
        filename += chars[dis(gen)];
    }
    return filename;
}

void CleanUsnJournal() {
    EnableSystemTimePrivilege();
    SYSTEMTIME st;
    GetSystemTime(&st);
    SYSTEMTIME originalTime = st;
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
        return;
    }
    std::string command = "fsutil usn deletejournal /d /n c:";
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        SetSystemTime(&originalTime);
        return;
    }
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring filePath = std::wstring(tempPath) + GenerateRandomFileName();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        WriteFile(hFile, "Dummy data", 10, &bytesWritten, NULL);
        FILETIME creationTime, lastWriteTime, lastAccessTime;
        GetFileTime(hFile, &creationTime, &lastAccessTime, &lastWriteTime);
        SYSTEMTIME writeTime;
        FileTimeToSystemTime(&lastWriteTime, &writeTime);
        char timeBuffer[256];
        sprintf_s(timeBuffer, "Dummy file write time: %04d-%02d-%02d %02d:%02d:%02d",
            writeTime.wYear, writeTime.wMonth, writeTime.wDay,
            writeTime.wHour, writeTime.wMinute, writeTime.wSecond);
        CloseHandle(hFile);
        DeleteFileW(filePath.c_str());
    }
    Sleep(1000);
    SetSystemTime(&originalTime);
}