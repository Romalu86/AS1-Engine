#pragma once


#include <cstdio>
#include <cstddef>
#include <string>

namespace as1::core::file_stream_p
{
    struct OpenModeFlags
    {
        bool readable = false;
        bool writable = false;
        bool append = false;
        bool truncate = false;
        bool update = false;
    };

    OpenModeFlags DecodeOpenMode(const char* modeText);
    std::FILE* OpenFile(const char* path, const char* modeText);
    std::size_t FileLength(std::FILE* file);
    std::size_t TellFile(std::FILE* file);
    bool SeekFile(std::FILE* file, std::size_t position);
}
