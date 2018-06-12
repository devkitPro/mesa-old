#pragma once

#include "util/u_debug.h"

#ifdef DEBUG
#	define TRACE(x...) debug_printf("nvnx: " x)
#  define STEP() debug_printf("%s:%d" __PRETTY_FUNCTION__, __LINE__);
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#  define STEP()
#  define CALLED()
#endif
