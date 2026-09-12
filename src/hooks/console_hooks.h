#pragma once

#define _WINSOCKAPI_
#include <windows.h>
#include <winsock2.h>

#include "core/runtime.h"

BOOL WINAPI Hooked_WriteConsoleA(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved);
BOOL WINAPI Hooked_WriteConsoleW(HANDLE hConsoleOutput, const VOID *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved);
BOOL WINAPI Hooked_WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
void WINAPI Hooked_OutputDebugStringA(LPCSTR lpOutputString);
void WINAPI Hooked_OutputDebugStringW(LPCWSTR lpOutputString);
DWORD WINAPI InitializeConsoleHooks(LPVOID lpParam);
