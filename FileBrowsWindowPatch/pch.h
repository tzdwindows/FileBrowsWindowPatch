#pragma once
#define NOMINMAX
#include <Windows.h>
#include <algorithm> // for std::min/max

#include <detours.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <map>
#include <string>
#include <functional>
#include <mutex>
#include <memory>
#include <comdef.h>
#include <winrt/base.h>

#include "HookManager.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "detours.lib")
#pragma comment(lib, "uxtheme.lib")

extern HMODULE g_hModule;
__declspec(dllexport) void WINAPI SetRemote_Config(Remote_Config* pRemoteConfig);