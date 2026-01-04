#pragma once

#if defined( PROFILE )
#define TRACY_ENABLE
#endif

#define TracyLine TracyConcat( __LINE__, U )

#define WIN32_LEAN_AND_MEAN
#include <tracy/Tracy.hpp>

#include <pix3.h>
