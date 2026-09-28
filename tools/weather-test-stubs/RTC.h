#pragma once
#include <ctime>
inline time_t gm_mktime(const tm *value)
{
    tm copy = *value;
#ifdef _WIN32
    return _mkgmtime(&copy);
#else
    return timegm(&copy);
#endif
}
