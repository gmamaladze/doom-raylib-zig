// Shim for <values.h> which doesn't exist on macOS.
// Provides the constants that DOOM's m_bbox.h and doomtype.h expect.
#ifndef __COMPAT_VALUES_H__
#define __COMPAT_VALUES_H__

#include <limits.h>
#include <float.h>

#ifndef MAXINT
#define MAXINT      INT_MAX
#endif

#ifndef MININT
#define MININT      INT_MIN
#endif

#ifndef MAXLONG
#define MAXLONG     LONG_MAX
#endif

#ifndef MINLONG
#define MINLONG     LONG_MIN
#endif

#ifndef MAXSHORT
#define MAXSHORT    SHRT_MAX
#endif

#ifndef MINSHORT
#define MINSHORT    SHRT_MIN
#endif

#ifndef MAXCHAR
#define MAXCHAR     CHAR_MAX
#endif

#ifndef MINCHAR
#define MINCHAR     CHAR_MIN
#endif

#endif // __COMPAT_VALUES_H__
