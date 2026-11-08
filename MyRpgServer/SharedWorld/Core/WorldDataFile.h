#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>

#if defined(PLATFORM_ANDROID) && PLATFORM_ANDROID
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Containers/StringConv.h"
#include "Templates/UniquePtr.h"
#endif

namespace shared_world
{
inline std::filesystem::path ResolveWorldDataPath(const std::filesystem::path& path)
{
#if defined(PLATFORM_ANDROID) && PLATFORM_ANDROID
    // Keep Unreal's virtual content path so the Android platform file can find it in the OBB.
    return path;
#else
    return std::filesystem::absolute(path);
#endif
}

template <class Buffer, class IOError = std::invalid_argument>
Buffer ReadWorldDataFile(const std::filesystem::path& path, std::size_t minimum, std::size_t maximum,
                         const char* unavailable, const char* outsideBudget, const char* readFailed)
{
#if defined(PLATFORM_ANDROID) && PLATFORM_ANDROID
    const auto NAME = path.generic_string();
    TUniquePtr<IFileHandle> file(FPlatformFileManager::Get().GetPlatformFile().OpenRead(UTF8_TO_TCHAR(NAME.c_str())));
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
#endif
    if (!file)
    {
        throw IOError(unavailable);
    }

#if defined(PLATFORM_ANDROID) && PLATFORM_ANDROID
    const auto LENGTH = file->Size();
#else
    const auto LENGTH = file.tellg();
#endif
    if (LENGTH < 0 || static_cast<std::size_t>(LENGTH) < minimum || static_cast<std::size_t>(LENGTH) > maximum)
    {
        throw std::invalid_argument(outsideBudget);
    }

    Buffer bytes;
    bytes.resize(static_cast<std::size_t>(LENGTH));
#if defined(PLATFORM_ANDROID) && PLATFORM_ANDROID
    const bool READ = bytes.empty() || file->Read(reinterpret_cast<uint8*>(bytes.data()), LENGTH);
#else
    file.seekg(0);
    const bool READ = static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), LENGTH)) &&
                      file.peek() == std::char_traits<char>::eof();
#endif
    if (!READ)
    {
        throw IOError(readFailed);
    }

    return bytes;
}

}

