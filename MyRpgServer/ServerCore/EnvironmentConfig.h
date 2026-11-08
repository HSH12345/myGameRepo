#pragma once

#include "StructuredLogger.h"

#include <cstdlib>
#include <exception>
#include <string>

namespace RpgConfig
{
inline std::string GetString(const char* name, const char* fallback = nullptr)
{
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
    {
        std::string result(value);
        std::free(value);
        if (!result.empty())
        {
            return result;
        }

    }

    return fallback == nullptr ? std::string() : std::string(fallback);
}

inline unsigned int GetUnsigned(const char* name, unsigned int fallback, unsigned int minimum, unsigned int maximum)
{
    const std::string TEXT_VALUE = GetString(name);
    if (TEXT_VALUE.empty())
    {
        return fallback;
    }

    try
    {
        const unsigned long PARSED = std::stoul(TEXT_VALUE);
        if (PARSED >= minimum && PARSED <= maximum)
        {
            return static_cast<unsigned int>(PARSED);
        }

    }

    catch (const std::exception&)
    {
    }

    RPG_LOG_WARNING("Configuration", name << " is invalid; using " << fallback);
    return fallback;
}

}

