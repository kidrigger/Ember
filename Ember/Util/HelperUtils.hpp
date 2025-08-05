#pragma once

#include "Runtime.hpp"

#include <cassert>

#if not defined( RENDERDOC_COMPAT )
#define DEBUG_BREAK __debugbreak()
#else
#define DEBUG_BREAK
#endif

#define ERR_FAIL_RET_V( EXPR, RET_VALUE )                                                                              \
  do                                                                                                                   \
  {                                                                                                                    \
    HRESULT hres = ( EXPR );                                                                                           \
    if ( auto x = FAILED( hres ) )                                                                                     \
    {                                                                                                                  \
      _com_error err( hres );                                                                                          \
      LPCTSTR    err_msg = err.ErrorMessage();                                                                         \
      MessageBox( nullptr, L"Warn: " #EXPR, err_msg, MB_OK | MB_ICONWARNING );                                         \
      DEBUG_BREAK;                                                                                                     \
      return RET_VALUE;                                                                                                \
    }                                                                                                                  \
  }                                                                                                                    \
  while ( false )

#define ERR_FAIL_RET_F( EXPR )                                                                                         \
  do                                                                                                                   \
  {                                                                                                                    \
    HRESULT hres = ( EXPR );                                                                                           \
    if ( auto x = FAILED( hres ) )                                                                                     \
    {                                                                                                                  \
      _com_error err( hres );                                                                                          \
      LPCTSTR    err_msg = err.ErrorMessage();                                                                         \
      MessageBox( nullptr, L"Warn: " #EXPR, err_msg, MB_OK | MB_ICONWARNING );                                         \
      DEBUG_BREAK;                                                                                                     \
      return false;                                                                                                    \
    }                                                                                                                  \
  }                                                                                                                    \
  while ( false )

#define ERR_FAIL_RET( EXPR )                                                                                           \
  do                                                                                                                   \
  {                                                                                                                    \
    HRESULT hres = ( EXPR );                                                                                           \
    if ( auto x = FAILED( hres ) )                                                                                     \
    {                                                                                                                  \
      _com_error err( hres );                                                                                          \
      LPCTSTR    err_msg = err.ErrorMessage();                                                                         \
      MessageBox( nullptr, L"Warn: " #EXPR, err_msg, MB_OK | MB_ICONWARNING );                                         \
      DEBUG_BREAK;                                                                                                     \
      return hres;                                                                                                     \
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
#define ASSERT_M( x, MSG ) assert( x )

#define UNREACHABLE std::terminate()
#define UNREACHABLE_M( MSG ) std::terminate()

#define UNIMPLEMENTED std::terminate()
#define UNIMPLEMENTED_M( MSG ) std::terminate()
