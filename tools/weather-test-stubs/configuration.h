#pragma once
#include <ctime>
#ifdef _WIN32
inline tm *weatherLocaltime(const time_t *value, tm *out)
{
    const tm *result = std::localtime(value);
    if (!result)
        return nullptr;
    *out = *result;
    return out;
}
inline tm *weatherGmtime(const time_t *value, tm *out)
{
    const tm *result = std::gmtime(value);
    if (!result)
        return nullptr;
    *out = *result;
    return out;
}
#define localtime_r weatherLocaltime
#define gmtime_r weatherGmtime
#endif
