#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#include <intrin.h>

static inline long atomic_load_long(volatile long *ptr)
{
	return _InterlockedCompareExchange(ptr, 0, 0);
}

static inline void atomic_store_long(volatile long *ptr, long val)
{
	_InterlockedExchange(ptr, val);
}

static inline long atomic_increment_long(volatile long *ptr)
{
	return _InterlockedIncrement(ptr);
}
#else
static inline long atomic_load_long(volatile long *ptr)
{
	return __sync_val_compare_and_swap(ptr, 0, 0);
}

static inline void atomic_store_long(volatile long *ptr, long val)
{
	__sync_synchronize();
	*ptr = val;
}

static inline long atomic_increment_long(volatile long *ptr)
{
	return __sync_fetch_and_add(ptr, 1) + 1;
}
#endif

#ifdef __cplusplus
}
#endif
