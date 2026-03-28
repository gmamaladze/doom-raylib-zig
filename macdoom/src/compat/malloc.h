// Shim for <malloc.h> which doesn't exist on macOS.
// On macOS, malloc() and friends live in <stdlib.h>.
#ifndef __COMPAT_MALLOC_H__
#define __COMPAT_MALLOC_H__

#include <stdlib.h>

#endif // __COMPAT_MALLOC_H__
