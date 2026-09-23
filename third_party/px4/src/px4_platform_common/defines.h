/// Stand-in for the PX4 platform header the EKF and matrix library include.
#ifndef EFIS_PX4_PLATFORM_COMMON_DEFINES_H
#define EFIS_PX4_PLATFORM_COMMON_DEFINES_H

#include <cmath>
#include <cstdint>

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
#ifndef M_TWOPI_F
#define M_TWOPI_F (2.f * M_PI_F)
#endif
#ifndef M_PI_2_F
#define M_PI_2_F 1.57079632679489661923f
#endif

#define PX4_ISFINITE(x) std::isfinite(x)
#define PX4_OK 0
#define PX4_ERROR (-1)

#ifndef PX4_INFO
#define PX4_INFO(...)
#endif
#ifndef PX4_WARN
#define PX4_WARN(...)
#endif
#ifndef PX4_ERR
#define PX4_ERR(...)
#endif
#ifndef PX4_DEBUG
#define PX4_DEBUG(...)
#endif

#endif
