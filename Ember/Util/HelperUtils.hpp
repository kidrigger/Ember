#pragma once

#include "Runtime.hpp"

#include <cassert>

#define DEBUG_BREAK __debugbreak()

#define ERR_FAIL_RET( hr )                                                                                             \
  do                                                                                                                   \
  {                                                                                                                    \
    HRESULT hres = ( hr );                                                                                             \
    if ( auto x = FAILED( hr ) )                                                                                       \
    {                                                                                                                  \
      _com_error err( hr );                                                                                            \
      LPCTSTR    err_msg = err.ErrorMessage();                                                                         \
      MessageBox( nullptr, L"Warn: " #hr, err_msg, MB_OK | MB_ICONWARNING );                                           \
      DEBUG_BREAK;                                                                                                     \
      return x;                                                                                                        \
    }                                                                                                                  \
  }                                                                                                                    \
  while ( false )

#define ERR_ABORT( hr )                                                                                                \
  do                                                                                                                   \
  {                                                                                                                    \
    HRESULT hres = ( hr );                                                                                             \
    if ( auto x = FAILED( hres ) )                                                                                     \
    {                                                                                                                  \
      _com_error err( hr );                                                                                            \
      LPCTSTR    err_msg = err.ErrorMessage();                                                                         \
      MessageBox( nullptr, L"Err: " #hr, err_msg, MB_OK | MB_ICONERROR );                                              \
      DEBUG_BREAK;                                                                                                     \
      exit( x );                                                                                                       \
    }                                                                                                                  \
  }                                                                                                                    \
  while ( false )

#define ASSERT( x ) assert( x )
#define ASSERT_M( x, MSG ) ASSERT( ( x ) and ( MSG ) )

#define COUNTOF( arr ) _countof( arr )
