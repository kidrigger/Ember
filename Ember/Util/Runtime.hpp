#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <shellapi.h>

// remove arithmetic macros
#if defined( max )
#undef max
#endif

#if defined( min )
#undef min
#endif

// We don't use these macros. Keep some cleaned up func-names.
#if defined( CreateWindow )
#undef CreateWindow
#endif

// The world here works on COM.
#include <wrl.h>

using Microsoft::WRL::ComPtr;
