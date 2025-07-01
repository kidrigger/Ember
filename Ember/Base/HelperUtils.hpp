#pragma once

#include "Runtime.hpp"

#include <cassert>

#define ERR_FAIL_RET( hr )                                                                                             \
  do                                                                                                                   \
  {                                                                                                                    \
    if ( auto x = FAILED( hr ) )                                                                                       \
    {                                                                                                                  \
      __debugbreak();                                                                                                  \
      return x;                                                                                                        \
    }                                                                                                                  \
  }                                                                                                                    \
  while ( false )

#define ERR_ABORT( hr )                                                                                                \
  do                                                                                                                   \
  {                                                                                                                    \
    if ( auto x = FAILED( hr ) )                                                                                       \
    {                                                                                                                  \
      __debugbreak();                                                                                                  \
      exit( x );                                                                                                       \
    }                                                                                                                  \
  }                                                                                                                    \
  while ( false )

#define ASSERT( x ) assert( x )
#define ASSERT_M( x, MSG ) ASSERT( ( x ) and ( MSG ) )

#define COUNTOF( arr ) _countof( arr )
