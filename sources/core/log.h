#pragma once
#include "types.h"
#include <cstdarg>
#include <cstdint>

namespace as1
{
    class FileLogger;

    // 0x00407210: formatted write-line plus message-box helper. Original callers pass g_fileLogger.
    std::intptr_t logAndShowError(FileLogger* logger, const char* format, ...);

    // 0x004072A0: formatted rewrite helper. Original callers pass g_fileLogger.
    std::intptr_t rewriteLogLine(FileLogger* logger, const char* format, ...);

    // 0x00407320: formatted write-line helper. Original callers pass g_fileLogger.
    std::intptr_t writeLogLine(FileLogger* logger, const char* format, ...);

    // 0x00407380: fatal formatted logger helper. Original callers pass g_fileLogger.
    [[noreturn]] void fatalLogError(FileLogger* logger, const char* format, ...);

    // 0x004073E0: resource-error formatter with jump-table suffix route.
    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);

    namespace LOG
    {
        bool OpenErrorLog(bool rewriteLog);
        void CloseErrorLog();
        void Write(const char* format, ...);
        void WriteV(const char* format, va_list args);
        void Rewrite(const char* format, ...);
        void RewriteV(const char* format, va_list args);
        void ShowMessage(const char* format, ...);
        [[noreturn]] void Fatal(const char* format, ...);
        void ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
    }
}
