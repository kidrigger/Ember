#pragma once

#if defined( NDEBUG ) or defined( RELDEBUG )
#define TRACY_ENABLE
#endif

#define TracyLine TracyConcat( __LINE__, U )

#define WIN32_LEAN_AND_MEAN
#include <tracy/Tracy.hpp>
