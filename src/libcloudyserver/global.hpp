#pragma once

#include <cloudy/global.hpp>

#if defined(CLOUDYSERVER_LIBRARY)
#define CLOUDYSERVERSHARED_EXPORT CLOUDY_EXPORT
#else
#define CLOUDYSERVERSHARED_EXPORT CLOUDY_IMPORT
#endif

