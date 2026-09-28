#pragma once
#include <cstdlib>
constexpr unsigned MALLOC_CAP_SPIRAM = 0, MALLOC_CAP_8BIT = 0;
inline void *heap_caps_malloc(size_t size, unsigned)
{
    return std::malloc(size);
}
inline void heap_caps_free(void *ptr)
{
    std::free(ptr);
}
inline void *heap_caps_realloc(void *ptr, size_t size, unsigned)
{
    return std::realloc(ptr, size);
}
