#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2

inline void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void heap_caps_free(void* pointer) { free(pointer); }
inline uint32_t heap_caps_get_free_size(uint32_t) { return 256 * 1024; }

