#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace RpgAuthentication
{
inline bool IsSafeUrl(const std::string& url)
{
    const std::size_t SCHEME_END = url.find("://");
    if (SCHEME_END == std::string::npos)
    {
        return false;
    }

    std::string scheme = url.substr(0, SCHEME_END);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char value)
                   {
                       return static_cast<char>(std::tolower(value));
                   });

    const bool IS_HTTPS = scheme == "https";
    if (!IS_HTTPS && scheme != "http")
    {
        return false;
    }

    const std::size_t AUTHORITY_BEGIN = SCHEME_END + 3;
    const std::size_t AUTHORITY_END = url.find_first_of("/?#", AUTHORITY_BEGIN);
    std::string authority = url.substr(AUTHORITY_BEGIN, AUTHORITY_END - AUTHORITY_BEGIN);
    if (authority.empty() || authority.find('@') != std::string::npos)
    {
        return false;
    }

    std::string host;
    if (authority.front() == '[')
    {
        const std::size_t BRACKET = authority.find(']');
        if (BRACKET == std::string::npos)
        {
            return false;
        }

        host = authority.substr(1, BRACKET - 1);
        if (BRACKET + 1 < authority.size() && authority[BRACKET + 1] != ':')
        {
            return false;
        }

    }
    else
    {
        const std::size_t COLON = authority.find(':');
        host = authority.substr(0, COLON);
    }

    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char value)
                   {
                       return static_cast<char>(std::tolower(value));
                   });

    if (host.empty())
    {
        return false;
    }

    return IS_HTTPS || host == "localhost" || host == "127.0.0.1" || host == "::1";
}

}

