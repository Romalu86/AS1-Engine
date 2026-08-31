#include "script/lgc_script.h"
#include "script/native_function_codes.h"
#include "script/vid_data_codes.h"

#include "core/application.h"
#include "base_sprite_list.h"
#include "core/configuration.h"
#include "core/file_stream.h"
#include "core/file_logger.h"
#include "core/log.h"
#include "core/profile_p.h"
#include "core/msvc_array_helpers.h"
#include "core/retail_stack_abi.h"
#include "mouse.h"
#include "input.h"
#include "map.h"
#include "engine.h"
#include "graph.h"
#include "core/weak_controller.h"
#include "sprite.h"
#include "sprite_collector_hash.h"
#include "sound/engine.h"
#include "vid/vid.h"
#ifdef _WIN32
#include "win/application_win.h"
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <string>
#if defined(_MSC_VER)
#include <io.h>
#endif

namespace as1
{

    namespace
    {
        std::uint32_t retailFileLength32(const FileStream& stream) noexcept
        {
            // Retail SCRIPT loaders call the CRT _filelength directly and keep
            // its raw signed-32 return in EAX/ESI.  In particular, -1 is not
            // sanitized to zero before the subsequent 32-bit allocation and
            // pointer arithmetic.  Keep that contract local to these owners;
            // the host convenience FileStream::length() intentionally remains
            // a safer API for non-retail callers.
#if defined(_MSC_VER)
            const std::FILE* file = stream.nativeFile();
            if (!file)
                return 0xFFFFFFFFu;
            const int fd = _fileno(const_cast<std::FILE*>(file));
            if (fd < 0)
                return 0xFFFFFFFFu;
            return static_cast<std::uint32_t>(_filelength(fd));
#else
            return static_cast<std::uint32_t>(stream.length());
#endif
        }

        const char Class[] = "";

        STRING lpFile;

        void shutdownGlobalScriptPathString() noexcept
        {
            destroyStringStorage(lpFile);
        }

        struct RetailLpFileShutdownRegistration
        {
            ~RetailLpFileShutdownRegistration() noexcept
            {
                shutdownGlobalScriptPathString();
                // Host-only double-destruction barrier: the retail owner above
                // deliberately leaves the released pointer stale, while the C++
                // STRING member destructor would otherwise release it again.
                lpFile.ResetSharedEmptyWithoutRelease();
            }
        };

        RetailLpFileShutdownRegistration g_retailLpFileShutdownRegistration;

        
        int g_scriptUnitIteratorCursor = 0;
        int g_scriptUnitIteratorTypeMask = 0;
        int g_scriptSpriteIteratorPass = 0;
        int g_scriptSpriteIteratorCursor = 0;

        std::FILE* scriptNativeFileFromInt(int value)
        {
            return reinterpret_cast<std::FILE*>(static_cast<std::intptr_t>(value));
        }

        int scriptNativeIntFromFile(std::FILE* file)
        {
            return static_cast<int>(reinterpret_cast<std::intptr_t>(file));
        }

        const input::InputMessageState& scriptApplicationInputState230() noexcept
        {
            if (void* const owner = core::ApplicationPhysicalOwner())
                return *reinterpret_cast<const input::InputMessageState*>(
                    static_cast<const std::uint8_t*>(owner) + 0x230u);
            static const input::InputMessageState zero{};
            return zero;
        }


        void scriptNativeDecodeGammaIndex(int value, std::uint32_t& diffuse, std::uint32_t& specular)
        {
            diffuse = 0;
            specular = 0;
            const std::uint32_t packed = static_cast<std::uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t byteValue = (packed >> shift) & 0xFFu;
                const std::uint32_t component = ((byteValue & 0x80u) != 0)
                    ? (((~byteValue) & 0x7Fu) << 1)
                    : ((byteValue & 0x7Fu) << 1);
                if ((byteValue & 0x80u) != 0)
                    specular |= (component & 0xFFu) << shift;
                else
                    diffuse |= (component & 0xFFu) << shift;
            }
        }

        int interpolateGammaComponent(int a1, int a2, int time)
        {
            int first = a1;
            if (first >= 0x80)
                first -= 0xFE;
            int second = a2;
            if (second >= 0x80)
                second -= 0xFE;
            // Retail IMUL keeps the low 32 bits even when the mathematical
            // product overflows signed int.  Avoid C++ signed-overflow UB so
            // arbitrary script time values retain the x86 two's-complement result.
            const std::uint32_t rawDelta =
                static_cast<std::uint32_t>(second - first) * static_cast<std::uint32_t>(time);
            std::int32_t delta = 0;
            std::memcpy(&delta, &rawDelta, sizeof(delta));
            const int step = delta / 255;
            return (step + first) & 0xFF;
        }

        int interpolateGammaColor(int g1, int g2, int time)
        {
            const int b0 = interpolateGammaComponent(g1 & 0xFF, g2 & 0xFF, time) & 0xFF;
            const int b1 = interpolateGammaComponent((g1 >> 8) & 0xFF, (g2 >> 8) & 0xFF, time) & 0xFF;
            const int b2 = interpolateGammaComponent((g1 >> 16) & 0xFF, (g2 >> 16) & 0xFF, time) & 0xFF;
            return b0 | (b1 << 8) | (b2 << 16);
        }

        int scriptNativeEncodeGammaIndex(std::uint32_t diffuse, std::uint32_t specular)
        {
            std::uint32_t out = (diffuse >> 1) & 0x7F7F7F7Fu;
            for (int shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t specByte = (specular >> shift) & 0xFFu;
                if (specByte == 0)
                    continue;
                const std::uint32_t packedByte = 0x80u | (((~specByte) & 0xFEu) >> 1);
                out = (out & ~(0xFFu << shift)) | ((packedByte & 0xFFu) << shift);
            }
            return static_cast<int>(out);
        }
    }


    int SCRIPT::compileScriptSourceFile(const STRING& scriptFile, const STRING& gameRoot)
    {
        (void)gameRoot;

        FileStream stream(scriptFile.str(), "rb");
        resetScriptVmState();
        assignStringFromString(host().m_scriptFile, scriptFile);
        m_physical.scriptFileToken = retailPointerToken(host().m_scriptFile.c_str());
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        const std::uint32_t fileLength = retailFileLength32(stream);
        // Retail allocates both raw owners before fread and reads the source
        // directly into [SCRIPT+0x44]+0xFE2.  Do not stage through a zeroed
        // vector: unread bytes after a short fread are allocator residue.
        prepareSourceCompiler(scriptFile, &stream, fileLength);

        int status = compileNextSourceItem();
        while (status == 0)
            status = compileNextSourceItem();

        writeLogLine(
            g_fileLogger,
            "LoadScript::ByteCode=%i varNo=%i DefineNo=%i stackNo=%i",
            m_physical.bytecodeEnd,
            functionCount(),
            defineCount(),
            executionStackCount());

        clearDefines();

        if (m_physical.bytecodeEnd != 0)
        {
            if (m_physical.bytecodeEnd > static_cast<int>(TemporaryBytecodeCapacity))
            {
                reportCompileError(2, "byte code size", m_physical.bytecodeEnd);
            }

            // Retail always replaces the 0x3E800 temporary owner with a
            // freshly allocated buffer of exactly bytecodeEnd bytes when
            // bytecodeEnd is non-zero, including the full 0x3E800 case and
            // the post-diagnostic oversized path.
            try
            {
                const std::size_t finalSize = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(m_physical.bytecodeEnd));
                script::RetailByteBuffer finalBytecode(finalSize);
                if (finalSize != 0)
                    std::memcpy(finalBytecode.data(), host().m_bytecode.data(), finalSize);
                host().m_bytecode.swap(finalBytecode);
                syncPhysicalBackingPointers();
            }
            catch (...)
            {
                // Failed exact-bytecode load leaves the compiled buffer empty.
                // allocation reports compiler error 2 / "tmp" and exits.
                reportCompileError(2, "tmp", 0);
                std::exit(1);
            }
        }
        else
        {
            resetScriptVmState();
        }

        host().m_sourceBuffer.clear();
        script::RetailByteBuffer().swap(host().m_sourceBuffer);
        host().m_portableSourceCursorOffset = 0;
        host().m_portableSourceEndOffset = 0;
        m_physical.sourceBufferToken = 0;
        // Retail keeps the source FILE open through compilation and closes it
        // only after releasing [SCRIPT+0x44].
        stream.close();
        return 0;
    }

    int SCRIPT::loadScriptFile(const STRING& scriptFile, const STRING& gameRoot)
    {
        std::uint32_t firstDword = static_cast<std::uint32_t>(m_physical.stackCount);
        FileStream stream(scriptFile.str(), "rb");
        resetScriptVmState();
        assignStringFromString(host().m_scriptFile, scriptFile);
        m_physical.scriptFileToken = retailPointerToken(host().m_scriptFile.c_str());
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        stream.read_new(&firstDword, sizeof(firstDword));

        if ((firstDword & 0xFF000000u) != 0)
        {
            // Retail keeps this probe FILE open while compileScriptSourceFile opens and
            // compiles the source through [SCRIPT+0x30], then closes the probe.
            const int result = compileScriptSourceFile(host().m_scriptFile, gameRoot);
            stream.close();
            return result;
        }

        const int stackCount = static_cast<int>(firstDword);

        if (m_physical.stackCapacity < static_cast<int>(InitialListCapacity))
        {
            try
            {
                host().m_executionStack.resize(InitialListCapacity);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                    static_cast<int>(InitialListCapacity));
            }
            m_physical.stackCapacity = static_cast<int>(InitialListCapacity);
            syncPhysicalBackingPointers();
        }
        m_physical.stackCount = stackCount;
        if (stackCount > m_physical.stackCapacity)
        {
            try
            {
                host().m_executionStack.resize(static_cast<std::size_t>(stackCount));
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", stackCount);
            }
            m_physical.stackCapacity = stackCount;
            syncPhysicalBackingPointers();
        }
        readExecutionStackFromStream(&stream);

        std::uint32_t bytecodeSize = 0;
        stream.read_new(&bytecodeSize, sizeof(bytecodeSize));

        host().m_bytecode.clear();
        script::RetailByteBuffer().swap(host().m_bytecode);
        try
        {
            if (bytecodeSize != 0)
            {
                // Raw operator-new storage: unread bytes after a short fread
                // must stay allocator residue, not vector-initialized zeroes.
                host().m_bytecode.resize(static_cast<std::size_t>(bytecodeSize));
            }
            else
            {
                // Retail still calls operator new(0), stores the returned
                // pointer at SCRIPT+0x34 and tests it for allocation failure.
                host().m_zeroLengthBytecodeAllocation = ::operator new(0);
            }
        }
        catch (...)
        {
            reportCompileError(2, "data2", 0);
            std::exit(1);
        }
        m_physical.bytecodeEnd = static_cast<int>(bytecodeSize);
        syncPhysicalBackingPointers();
        if (bytecodeSize != 0)
            stream.read(host().m_bytecode.data(), bytecodeSize);

        stream.close();
        return 0;
    }

    int SCRIPT::writeExecutionStackToStream(BaseStream* stream)
    {
        int result = m_physical.stackCount;
        for (int i = 0; i < m_physical.stackCount; ++i)
        {
            host().m_executionStack[static_cast<std::size_t>(i)].writeToStream(stream);
            result = m_physical.stackCount;
        }
        return result;
    }

    int SCRIPT::readExecutionStackFromStream(BaseStream* stream)
    {
        int result = m_physical.stackCount;
        for (int i = 0; i < m_physical.stackCount; ++i)
        {
            host().m_executionStack[static_cast<std::size_t>(i)].readFromStream(stream);
            result = m_physical.stackCount;
        }
        return result;
    }

    void SCRIPT::clearExecutionStack()
    {
        m_physical.stackCapacity = 0;
        m_physical.stackCount = 0;
#if defined(_WIN32) && defined(_M_IX86)
        script::StackObject* const records = host().m_executionStack.detachRetailAllocation();
        if (records)
            msvcStringRecordDeletingDestructor(static_cast<void*>(records), 3);
#else
        host().m_executionStack.clear();
        script::RetailRawArray<script::StackObject>().swap(host().m_executionStack);
#endif
        m_physical.stackTableToken = 0;
    }

    void SCRIPT::pushExecutionInt(int value)
    {
        script::StackObject obj;
        obj.assignInt(value);
        appendExecutionStackObject(obj);
    }

    void SCRIPT::pushExecutionString(const STRING& value)
    {
        script::StackObject obj;
        obj.assignString(value);
        appendExecutionStackObject(obj);
    }

    int SCRIPT::executionStackCount() const
    {
        return m_physical.stackCount;
    }

    int SCRIPT::executionStackCapacity() const
    {
        return m_physical.stackCapacity;
    }

    const script::StackObject* SCRIPT::executionStackAt(int index) const
    {
        if (index < 0 || index >= m_physical.stackCount ||
            index >= m_physical.stackCapacity ||
            static_cast<std::size_t>(index) >= host().m_executionStack.size())
            return nullptr;
        return &host().m_executionStack[static_cast<std::size_t>(index)];
    }

    script::StackObject* SCRIPT::mutableExecutionStackAt(int index)
    {
        if (index < 0 || index >= m_physical.stackCount ||
            index >= m_physical.stackCapacity ||
            static_cast<std::size_t>(index) >= host().m_executionStack.size())
            return nullptr;
        return &host().m_executionStack[static_cast<std::size_t>(index)];
    }

    script::StackObject* SCRIPT::mutableExecutionStackStorageAt(int index)
    {
        // Retail stack owners address [SCRIPT+0x0C] + index*12 directly.
        // Do not add active-count/capacity/null guards here: malformed stack
        // state must retain the original raw-access/fault semantics.
        return &host().m_executionStack[static_cast<std::size_t>(index)];
    }

    void SCRIPT::growExecutionStackForAppend()
    {
        const int oldCapacity = m_physical.stackCapacity;
        if (m_physical.stackCount < oldCapacity)
            return;

        const int newCapacity = oldCapacity * 2 + 4;
        if (newCapacity <= oldCapacity)
            return;

        script::RetailRawArray<script::StackObject> newItems;
        try
        {
            newItems.resize(static_cast<std::size_t>(newCapacity));
        }
        catch (...)
        {
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
        }
        for (int i = 0; i < oldCapacity; ++i)
            newItems[static_cast<std::size_t>(i)].copyStorageFrom(host().m_executionStack[static_cast<std::size_t>(i)]);
        host().m_executionStack.swap(newItems);
        m_physical.stackCapacity = newCapacity;
        m_physical.stackTableToken = host().m_executionStack.empty()
            ? 0u : retailPointerToken(host().m_executionStack.data());
    }

    int SCRIPT::clearSpriteReferencesFromExecutionStack(SPRITE* sprite)
    {
        const int target = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu));
        int cleared = 0;
        for (int i = 0; i < m_physical.stackCount; ++i)
        {
            script::StackObject& value = host().m_executionStack[static_cast<std::size_t>(i)];
            if ((value.flags & script::STACK_OBJECT_REF) != 0 && value.intValue == target)
            {
                value.intValue = 0;
                value.flags = static_cast<std::uint8_t>(value.flags & ~script::STACK_OBJECT_REF);
                ++cleared;
            }
        }
        return cleared;
    }

    void SCRIPT::appendExecutionStackObject(const script::StackObject& value)
    {
        script::StackObject localCopy;
        localCopy.copyStorageFrom(value);
        appendExecutionStackRecord(localCopy.flags, localCopy.intValue, localCopy.text);
    }

    void SCRIPT::appendExecutionStackRecord(std::uint8_t flags, int value, const STRING& text)
    {
        growExecutionStackForAppend();

        host().m_executionStack[static_cast<std::size_t>(m_physical.stackCount)].assignFields(
            flags, value, text);
        ++m_physical.stackCount;
    }

    int SCRIPT::functionCount() const
    {
        return m_physical.functionCount;
    }

    int SCRIPT::functionCapacity() const
    {
        return m_physical.functionCapacity;
    }

    const script::LogicFunctionList& SCRIPT::functionTable() const
    {
        return host().m_functionTable;
    }

    int SCRIPT::defineCount() const
    {
        return m_physical.defineCount;
    }

    int SCRIPT::defineCapacity() const
    {
        return m_physical.defineCapacity;
    }

    const script::RetailRawArray<ScriptDefinePairRecord>& SCRIPT::defineTable() const
    {
        return host().m_defines;
    }

    const STRING& SCRIPT::scriptFile() const
    {
        return host().m_scriptFile;
    }

    const script::RetailByteBuffer& SCRIPT::bytecodeBuffer() const
    {
        return host().m_bytecode;
    }

    int SCRIPT::bytecodeEnd() const
    {
        return m_physical.bytecodeEnd;
    }

    int SCRIPT::sourceCursorOffset() const
    {
        return host().m_portableSourceCursorOffset;
    }

    int SCRIPT::sourceEndOffset() const
    {
        return host().m_portableSourceEndOffset;
    }

    const script::RetailByteBuffer& SCRIPT::sourceBuffer() const
    {
        return host().m_sourceBuffer;
    }

    int SCRIPT::sourceLineNumber() const
    {
        return m_physical.sourceLine;
    }

    int SCRIPT::conditionalDepth() const
    {
        return m_physical.conditionalDepth;
    }

    int SCRIPT::fallbackFunctionIndex() const
    {
        return m_physical.fallbackFunction;
    }

    int SCRIPT::parseMode() const
    {
        return m_physical.parseMode;
    }

    void destroyScriptDefinePair(ScriptDefinePairRecord* self) noexcept
    {
        destroyStringStorage(self->value);
        destroyStringStorage(self->name);
    }

    void* scriptDefinePairDeletingDestructor(ScriptDefinePairRecord* self, unsigned char flags) noexcept
    {
#if defined(_WIN32) && defined(_M_IX86)
        if ((flags & 0x02u) != 0)
        {
            std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(self) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
                destroyScriptDefinePair(self + (i - 1u));
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(header));
            return header;
        }
#endif

        destroyScriptDefinePair(self);
        if ((flags & 0x01u) != 0)
            ::operator delete(static_cast<void*>(self));
        return self;
    }

    void SCRIPT::resetScriptVmState()
    {
        if (host().m_zeroLengthBytecodeAllocation)
        {
            ::operator delete(host().m_zeroLengthBytecodeAllocation);
            host().m_zeroLengthBytecodeAllocation = nullptr;
        }
        host().m_bytecode.clear();
        script::RetailByteBuffer().swap(host().m_bytecode);
        m_physical.bytecodeBufferToken = 0;

        host().m_sourceBuffer.clear();
        script::RetailByteBuffer().swap(host().m_sourceBuffer);
        host().m_portableSourceCursorOffset = 0;
        host().m_portableSourceEndOffset = 0;
        m_physical.sourceBufferToken = 0;

        clearExecutionStack();

        m_physical.functionCapacity = 0;
        m_physical.functionCount = 0;
        host().m_functionTable.clearRecords();
        m_physical.functionTableToken = 0;

        clearDefines();

        m_physical.bytecodeEnd = 0;
        m_physical.conditionalDepth = 0;
        m_physical.fallbackFunction = -1;
        m_physical.sourceCursor = 0;
        m_physical.parseMode = 0;
    }

    void SCRIPT::prepareSourceCompiler(const STRING& scriptFile, BaseStream* stream, std::uint32_t sourceSize)
    {
        (void)scriptFile;
        try
        {
            host().m_bytecode.clear();
            host().m_bytecode.resize(TemporaryBytecodeCapacity);
        }
        catch (...)
        {
            // Retail [SCRIPT+0x34] allocation failure: error 2 / "data", exit(1).
            reportCompileError(2, "data", 0);
            std::exit(1);
        }
        m_physical.bytecodeEnd = 0;
        syncPhysicalBackingPointers();

        try
        {
            host().m_sourceBuffer.clear();
            const std::uint32_t allocationSize = sourceSize + static_cast<std::uint32_t>(SourceBufferPadding);
            host().m_sourceBuffer.resize(static_cast<std::size_t>(allocationSize));
        }
        catch (...)
        {
            // Retail [SCRIPT+0x44] allocation failure: error 2 / "ini", exit(1).
            reportCompileError(2, "ini", 0);
            std::exit(1);
        }
        host().m_portableSourceCursorOffset = static_cast<int>(SourcePayloadOffset);
        const std::uint32_t sourceEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + sourceSize;
        host().m_portableSourceEndOffset = static_cast<std::int32_t>(sourceEndOffset);
        syncPhysicalBackingPointers();
        if (sourceSize != 0)
        {
            stream->read(host().m_sourceBuffer.data() + SourcePayloadOffset,
                static_cast<unsigned>(sourceSize));
        }
        syncPhysicalSourcePointers();

        if (executionStackCapacity() < static_cast<int>(InitialListCapacity))
        {
            try
            {
                host().m_executionStack.resize(InitialListCapacity);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                    static_cast<int>(InitialListCapacity));
            }
            m_physical.stackCapacity = static_cast<int>(InitialListCapacity);
            syncPhysicalBackingPointers();
        }
        m_physical.stackCount = 0;

        if (functionCapacity() < static_cast<int>(InitialListCapacity))
        {
            host().m_functionTable.reserveExact(InitialListCapacity);
            syncPhysicalFunctionList();
        }

        m_physical.sourceLine = 0;
        m_physical.conditionalDepth = 0;
        m_physical.parseMode = 0;
    }

    void SCRIPT::clearFunctionTable()
    {
        host().m_functionTable.clearRecords();
        syncPhysicalFunctionList();
    }

    void SCRIPT::appendFunctionRecord(const STRING& name, std::uint8_t flags, const STRING& text, int bytecodeStart0C, int stackBase10, int argCount14)
    {
        host().m_functionTable.append(name, flags, text, bytecodeStart0C, stackBase10, argCount14);
        syncPhysicalFunctionList();
    }

    void SCRIPT::clearDefines()
    {
        m_physical.defineCount = 0;
        m_physical.defineCapacity = 0;
#if defined(_WIN32) && defined(_M_IX86)
        ScriptDefinePairRecord* const records = host().m_defines.detachRetailAllocation();
        if (records)
            scriptDefinePairDeletingDestructor(records, 3);
#else
        host().m_defines.clear();
        script::RetailRawArray<ScriptDefinePairRecord>().swap(host().m_defines);
#endif
        m_physical.defineTableToken = 0;
    }

    int SCRIPT::findDefine(const STRING& name) const
    {
        // compileNextSourceItem #define/#undef scans the define table backwards.
        for (int i = static_cast<int>(host().m_defines.size()) - 1; i >= 0; --i)
        {
            if (std::strcmp(host().m_defines[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                return i;
        }
        return -1;
    }

    int SCRIPT::addOrReplaceDefine(const STRING& name, const STRING& value)
    {
        const int existing = findDefine(name);
        if (existing >= 0)
        {
            host().m_defines[static_cast<std::size_t>(existing)].value = value;
            return existing;
        }

        if (defineCount() >= m_physical.defineCapacity)
        {
            const int oldCapacity = m_physical.defineCapacity;
            const int capacity = oldCapacity * 2 + 4;
            try
            {
                host().m_defines.reserve(static_cast<std::size_t>(capacity));
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);
            }
            m_physical.defineCapacity = capacity;
            syncPhysicalDefineList();
        }

        ScriptDefinePairRecord rec;
        rec.name = name;
        rec.value = value;
        host().m_defines.push_back(rec);
        syncPhysicalDefineList();
        return m_physical.defineCount - 1;
    }

    int SCRIPT::undefine(const STRING& name)
    {
        const int index = findDefine(name);
        if (index < 0)
            return -1;

        host().m_defines.erase(host().m_defines.begin() + index);
        if (host().m_defines.empty())
            clearDefines();
        else
            syncPhysicalDefineList();
        return index;
    }

    int SCRIPT::rewriteDefineMacro(int tokenStartOffset, int tokenLength)
    {
        std::string token(reinterpret_cast<const char*>(host().m_sourceBuffer.data() + tokenStartOffset),
            static_cast<std::size_t>(tokenLength));

        int found = -1;
        for (int i = static_cast<int>(host().m_defines.size()) - 1; i >= 0; --i)
        {
            if (host().m_defines[static_cast<std::size_t>(i)].name.str() == token)
            {
                found = i;
                break;
            }
        }
        if (found < 0)
            return -1;

        const std::string value = host().m_defines[static_cast<std::size_t>(found)].value.str();
        const int valueLength = static_cast<int>(value.size());
        const int newCursor = tokenStartOffset + tokenLength - valueLength;
        setSourceCursorOffset(newCursor);
        if (valueLength > 0)
        {
            std::copy(value.begin(), value.end(),
                host().m_sourceBuffer.begin() + static_cast<std::ptrdiff_t>(newCursor));
        }
        return found;
    }

    std::uint8_t SCRIPT::sourceByteAtCursor() const
    {
        return host().m_sourceBuffer[static_cast<std::size_t>(host().m_portableSourceCursorOffset)];
    }

    void SCRIPT::setSourceCursorOffset(int offset)
    {
        host().m_portableSourceCursorOffset = offset;
        syncPhysicalSourcePointers();
    }

    void SCRIPT::reportCompileError(int errorCode, const char* detailText, int detailValue)
    {
        logFileLoggerResourceError(g_fileLogger,
            "LOGIC '%s' line %i",
            errorCode,
            detailText,
            detailValue,
            host().m_scriptFile.c_str(),
            m_physical.sourceLine + 1);

        if (m_physical.sourceCursor == 0)
            return;

        char window[61];
        const int start = host().m_portableSourceCursorOffset - 30;
        for (int i = 0; i < 60; ++i)
        {
            unsigned char c = host().m_sourceBuffer[static_cast<std::size_t>(start + i)];
            if (c == '\n' || c == '\r' || c == '\t')
                c = '?';
            window[i] = static_cast<char>(c);
        }
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);

        for (int i = 0; i < 60; ++i)
            window[i] = (i == 30) ? '^' : ' ';
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);
    }

    namespace
    {
        int retailCtypeInput41BC50(unsigned char c) noexcept
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            return static_cast<int>(static_cast<signed char>(c));
#else
            // Audit hosts keep the portable ctype precondition; Win32/x86 is
            // the authoritative route for the signed-byte CRT contract.
            return static_cast<int>(c);
#endif
        }

        bool isIdentifierStart(unsigned char c)
        {
            return std::isalpha(retailCtypeInput41BC50(c)) != 0 || c == '_';
        }

        bool isIdentifierChar(unsigned char c)
        {
            return std::isalnum(retailCtypeInput41BC50(c)) != 0 || c == '_';
        }

        bool isScriptWhitespace(unsigned char c)
        {
            return std::isspace(retailCtypeInput41BC50(c)) != 0;
        }
    }

    int SCRIPT::skipTriviaAndPreprocess()
    {
        int skipDepth = 0;
        int commentState = 0; // 0 none, 1 //, 2 /* */

        while (host().m_portableSourceCursorOffset < host().m_portableSourceEndOffset)
        {
            if (commentState != 0)
            {
                const unsigned char c = sourceByteAtCursor();
                if (commentState == 1)
                {
                    if (c == '\n')
                        commentState = 0;
                }
                else if (commentState == 2)
                {
                    if (c == '/' &&
                        host().m_sourceBuffer[static_cast<std::size_t>(host().m_portableSourceCursorOffset - 1)] == '*')
                        commentState = 0;
                }
                setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                if (c == '\n')
                    ++m_physical.sourceLine;
                continue;
            }

            unsigned char c = sourceByteAtCursor();

            if (isIdentifierStart(c) && m_physical.parseMode == 0)
            {
                const int tokenStart = host().m_portableSourceCursorOffset;
                int tokenLength = 0;
                while (isIdentifierChar(host().m_sourceBuffer[static_cast<std::size_t>(tokenStart + tokenLength)]))
                {
                    if (tokenLength >= 0x0FFF)
                    {
                        reportCompileError(10, "Very long name", 0);
                        std::exit(1);
                    }
                    ++tokenLength;
                }
                if (rewriteDefineMacro(tokenStart, tokenLength) >= 0)
                    continue;
            }

            if (c == '#')
            {
                const char* cur = reinterpret_cast<const char*>(host().m_sourceBuffer.data()) + host().m_portableSourceCursorOffset;

                if (std::strncmp(cur, "#ifdef", 6) == 0)
                {
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 6);
                    ++m_physical.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_physical.parseMode = 1;
                        readIdentifier(name);
                        m_physical.parseMode = 0;
                        if (host().m_functionTable.findLastByName(name) < 0 && findDefine(name) < 0)
                            skipDepth = m_physical.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#ifndef", 7) == 0)
                {
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 7);
                    ++m_physical.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_physical.parseMode = 1;
                        readIdentifier(name);
                        m_physical.parseMode = 0;
                        if (host().m_functionTable.findLastByName(name) >= 0 || findDefine(name) >= 0)
                            skipDepth = m_physical.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#endif", 6) == 0)
                {
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 6);
                    if (skipDepth == m_physical.conditionalDepth)
                        skipDepth = 0;
                    --m_physical.conditionalDepth;
                    if (m_physical.conditionalDepth < 0)
                        reportCompileError(10, "#endif without #ifdef", 0);
                    continue;
                }

                if (std::strncmp(cur, "#else", 5) == 0)
                {
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 5);
                    if (skipDepth == 0)
                    {
                        if (m_physical.conditionalDepth > 0)
                            skipDepth = m_physical.conditionalDepth;
                    }
                    else if (skipDepth == m_physical.conditionalDepth)
                        skipDepth = 0;

                    if (m_physical.conditionalDepth <= 0)
                        reportCompileError(10, "#else	without	#ifdef", 0);
                    continue;
                }
            }

            if (c == '/')
            {
                const std::size_t nextOff = static_cast<std::size_t>(host().m_portableSourceCursorOffset + 1);
                const unsigned char next = host().m_sourceBuffer[nextOff];
                if (next == '/')
                {
                    commentState = 1;
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                    continue;
                }
                if (next == '*')
                {
                    commentState = 2;
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                    continue;
                }
            }

            if (c == '?')
            {
                reportCompileError(10, "?: not supported in this version", 0);
                std::exit(1);
            }

            if (skipDepth == 0 && !isScriptWhitespace(c) && c != 0)
                return 0;

            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
            if (c == '\n')
                ++m_physical.sourceLine;
        }

        if (m_physical.conditionalDepth > 0)
            reportCompileError(10, "#ifdef without #endif", m_physical.conditionalDepth);
        return 1;
    }

    int SCRIPT::requireSourceToken()
    {
        if (skipTriviaAndPreprocess())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }
        return 0;
    }

    int SCRIPT::readSourceLine(STRING& outLine)
    {
        if (requireSourceToken())
            return 1;

        std::string line;
        while (true)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '\n' || c == '\r')
                break;
            if (line.size() >= 4095)
            {
                reportCompileError(10, "Very long line", 0);
                std::exit(1);
            }
            line.push_back(static_cast<char>(c));
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
        }

        if (line.empty())
        {
            reportCompileError(10, "empty line", 0);
            std::exit(1);
        }

        const std::size_t comment = line.find("//");
        if (comment != std::string::npos)
            line.erase(comment);

        const std::size_t last = line.find_last_not_of(" \n\r\t");
        if (last == std::string::npos)
            line.clear();
        else
            line.erase(last + 1);

        outLine = STRING(line);
        return requireSourceToken();
    }

    int SCRIPT::readIdentifier(STRING& outName)
    {
        if (requireSourceToken())
            return 0;
        std::string name;
        while (true)
        {
            const unsigned char c = sourceByteAtCursor();
            if (!isIdentifierChar(c))
                break;
            if (name.size() >= 4095)
            {
                reportCompileError(10, "Very long name", 0);
                std::exit(1);
            }
            name.push_back(static_cast<char>(c));
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
        }
        outName = STRING(name);
        if (name.empty())
        {
            reportCompileError(4, "name", 0);
            std::exit(1);
        }
        requireSourceToken();
        return static_cast<int>(name.size());
    }

    int SCRIPT::matchToken(const char* token)
    {
        const std::size_t len = std::strlen(token);
        requireSourceToken();
        const char* cur = reinterpret_cast<const char*>(host().m_sourceBuffer.data() + host().m_portableSourceCursorOffset);
        if (std::strncmp(cur, token, len) != 0)
            return 0;
        const unsigned char first = static_cast<unsigned char>(token[0]);
        const unsigned char after = static_cast<unsigned char>(cur[len]);
        if ((std::isalpha(first) || first == '#') && isIdentifierChar(after))
            return 0;
        setSourceCursorOffset(host().m_portableSourceCursorOffset + static_cast<int>(len));
        skipTriviaAndPreprocess();
        return 1;
    }

    int SCRIPT::requireToken(const char* token)
    {
        if (matchToken(token))
            return 1;
        reportCompileError(13, token, 0);
        std::exit(1);
    }

    int SCRIPT::parseConstantIntExpression()
    {
        requireSourceToken();
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileExpression();

        if (host().m_bytecode[static_cast<std::size_t>(bytecodeStart)] == 1 &&
            m_physical.bytecodeEnd - bytecodeStart == 5)
        {
            int value = 0;
            std::memcpy(&value,
                host().m_bytecode.data() + static_cast<std::size_t>(bytecodeStart + 1),
                sizeof(value));
            m_physical.bytecodeEnd -= 5;
            return value;
        }

        reportCompileError(4, "constant int value", 0);
        std::exit(1);
    }


    int SCRIPT::readQuotedStringLiteral(char* outText)
    {
        char* dst = outText;
        if (sourceByteAtCursor() != '"')
            return 0;

        setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
        if (sourceByteAtCursor() != '"')
        {
            while (true)
            {
                if (host().m_portableSourceCursorOffset >= host().m_portableSourceEndOffset)
                    break;

                unsigned char c = sourceByteAtCursor();
                if (c == '\\')
                {
                    const int nextOffset = host().m_portableSourceCursorOffset + 1;
                    const unsigned char next = host().m_sourceBuffer[static_cast<std::size_t>(nextOffset)];

                    if (next == '\r')
                    {
                        const int afterCrOffset = host().m_portableSourceCursorOffset + 2;
                        const unsigned char afterCr = host().m_sourceBuffer[static_cast<std::size_t>(afterCrOffset)];
                        if (afterCr == '\n')
                        {
                            setSourceCursorOffset(afterCrOffset);
                            ++m_physical.sourceLine;
                        }
                        else
                        {
                            setSourceCursorOffset(nextOffset);
                            *dst++ = static_cast<char>(next);
                        }
                    }
                    else if (next == '\n')
                    {
                        setSourceCursorOffset(nextOffset);
                        ++m_physical.sourceLine;
                    }
                    else if (next == 'n')
                    {
                        *dst++ = '\n';
                        setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                    }
                    else if (next == 'r')
                    {
                        *dst++ = '\r';
                        setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                    }
                    else
                    {
                        setSourceCursorOffset(nextOffset);
                        *dst++ = static_cast<char>(next);
                    }
                }
                else
                {
                    *dst++ = static_cast<char>(c);
                }

                setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                if (sourceByteAtCursor() == '"')
                    break;
            }
        }

        const int quoteOffset = host().m_portableSourceCursorOffset;
        setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
        if (quoteOffset >= host().m_portableSourceEndOffset)
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }

        *dst++ = '\0';
        return static_cast<int>(dst - outText);
    }

    int SCRIPT::setLastFunctionElementCount(int argCount)
    {
        host().m_functionTable.setLastValue2(argCount);
        return functionCount() * 3;
    }


    void SCRIPT::compileIntDeclaration()
    {
        int declaredCount = 0;
        int rawFlags = 0;
        STRING name;

        requireSourceToken();
        if (sourceByteAtCursor() == '*')
        {
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
            rawFlags = script::STACK_OBJECT_DYNAMIC;
        }

        readIdentifier(name);

        const auto& records = host().m_functionTable.items();
        for (int i = functionCount() - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "int redefinition '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t functionScratch10;
        std::uint32_t functionScratch14;
#else
        std::uint32_t functionScratch10 = 0;
        std::uint32_t functionScratch14 = 0;
#endif
        appendFunctionRecord(
            name, 1, STRING(), stackBase,
            static_cast<int>(core::retailReadStackDword(&functionScratch10)),
            static_cast<int>(core::retailReadStackDword(&functionScratch14)));

        auto appendIntStackRecord = [&](std::uint8_t extraFlags, int value, bool hasPayload) -> int
        {
            script::StackObject temp;
            temp.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), 0, STRING());
            appendExecutionStackObject(temp);
            const int index = executionStackCount() - 1;
            script::StackObject& record = host().m_executionStack[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>(record.flags | extraFlags);
            if (hasPayload)
            {
                record.intValue = value;
                record.flags = static_cast<std::uint8_t>(record.flags | script::STACK_OBJECT_HAS_PAYLOAD);
            }
            return index;
        };

        auto markUninitialized = [&](int index)
        {
            script::StackObject& record = host().m_executionStack[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>(record.flags | static_cast<std::uint8_t>(rawFlags) | script::STACK_OBJECT_CHAR_WRITE);
        };

        auto markInitialized = [&](int index, int value)
        {
            script::StackObject& record = host().m_executionStack[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>((record.flags & ~script::STACK_OBJECT_CHAR_WRITE) | script::STACK_OBJECT_HAS_PAYLOAD);
            record.intValue = value;
        };

        if (matchToken("["))
        {
            rawFlags |= script::STACK_OBJECT_ARRAY;
            if (sourceByteAtCursor() == ']')
            {
                setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                if (!matchToken("="))
                {
                    reportCompileError(10, "for [] need initialisation", 0);
                    std::exit(1);
                }
                requireToken("{");
                for (;;)
                {
                    const int index = appendIntStackRecord(static_cast<std::uint8_t>(rawFlags), 0, false);
                    const int value = parseConstantIntExpression();
                    markInitialized(index, value);
                    ++declaredCount;
                    if (!matchToken(","))
                        break;
                }
                requireToken("}");
                setLastFunctionElementCount(declaredCount);
                return;
            }

            declaredCount = parseConstantIntExpression();
            requireToken("]");
        }
        else
        {
            declaredCount = 1;
        }

        if (declaredCount > 0)
        {
            for (int i = 0; i < declaredCount; ++i)
            {
                const int index = appendIntStackRecord(0, 0, false);
                markUninitialized(index);
            }
        }

        if (matchToken("="))
        {
            if ((rawFlags & script::STACK_OBJECT_ARRAY) != 0)
            {
                requireToken("{");
                int initIndex = 0;
                if (declaredCount > 0)
                {
                    for (;;)
                    {
                        if (initIndex >= declaredCount)
                            break;
                        const int stackIndex = executionStackCount() - declaredCount + initIndex;
                        const int value = parseConstantIntExpression();
                        markInitialized(stackIndex, value);
                        ++initIndex;
                        if (!matchToken(","))
                            break;
                        if (initIndex >= declaredCount)
                        {
                            reportCompileError(10, "too many initializers", 0);
                            std::exit(1);
                        }
                    }
                }
                else
                {
                    reportCompileError(10, "too many initializers", 0);
                    std::exit(1);
                }
                requireToken("}");
            }
            else
            {
                const int stackIndex = executionStackCount() - 1;
                const int value = parseConstantIntExpression();
                markInitialized(stackIndex, value);
            }
        }

        setLastFunctionElementCount(declaredCount);
    }


    void SCRIPT::compileStringDeclaration()
    {
        int declaredCount = 0;
        int rawFlags = 0;
        STRING name;

        readIdentifier(name);

        const auto& records = host().m_functionTable.items();
        for (int i = functionCount() - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "string redifinition '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
        }

        if (matchToken("["))
        {
            rawFlags = script::STACK_OBJECT_ARRAY;
            declaredCount = parseConstantIntExpression();
            requireToken("]");
        }
        else
        {
            declaredCount = 1;
        }

        const int stackBase = executionStackCount();
#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t functionScratch10;
        std::uint32_t functionScratch14;
#else
        std::uint32_t functionScratch10 = 0;
        std::uint32_t functionScratch14 = 0;
#endif
        appendFunctionRecord(
            name, 1, STRING(), stackBase,
            static_cast<int>(core::retailReadStackDword(&functionScratch10)),
            static_cast<int>(core::retailReadStackDword(&functionScratch14)));

        auto appendStringStackRecord = [&](std::uint8_t extraFlags) -> int
        {
            // Retail compileStringDeclaration initializes the temporary stack object's flag
            // and AS_STRING field but leaves its +4 dword untouched.
#if defined(_MSC_VER) && defined(_M_IX86)
            std::uint32_t stringScratch04;
#else
            std::uint32_t stringScratch04 = 0;
#endif
            appendExecutionStackRecord(
                static_cast<std::uint8_t>(script::STACK_OBJECT_STRING),
                static_cast<int>(core::retailReadStackDword(&stringScratch04)),
                STRING());
            const int index = executionStackCount() - 1;
            script::StackObject& record = host().m_executionStack[static_cast<std::size_t>(index)];
            record.flags = static_cast<std::uint8_t>(record.flags | extraFlags);
            return index;
        };

        if (declaredCount > 0)
        {
            for (int i = 0; i < declaredCount; ++i)
                appendStringStackRecord(static_cast<std::uint8_t>(rawFlags));
        }

        if (matchToken("="))
        {
#if defined(_MSC_VER) && defined(_M_IX86)
            char literal[4096];
#else
            char literal[4096] = {};
#endif
            readQuotedStringLiteral(literal);

            const int stackIndex = executionStackCount() - 1;
            script::StackObject& record = host().m_executionStack[static_cast<std::size_t>(stackIndex)];
            record.text = STRING(literal);
            record.flags = static_cast<std::uint8_t>((record.flags | script::STACK_OBJECT_HAS_PAYLOAD) & ~script::STACK_OBJECT_CHAR_WRITE);
        }

        setLastFunctionElementCount(declaredCount);
    }


    int SCRIPT::compilePrimaryExpression()
    {
        std::uint8_t unaryOpcode = 0;
        std::uint8_t prefixOpcode = script::opcodeValue(script::VmOpcode::ReadVariable);

        const int cursorBeforeUnary = host().m_portableSourceCursorOffset;
        const bool nextIsMinus =
            host().m_sourceBuffer[static_cast<std::size_t>(cursorBeforeUnary + 1)] == '-';
        if (!nextIsMinus && matchToken("-"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::Negate);
        else if (matchToken("~"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::BitwiseNot);
        else if (matchToken("!"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::LogicalNot);

        if (matchToken("--"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreDecrement);
        else if (matchToken("++"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreIncrement);
        else if (matchToken("&"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::AddressOf);

        auto canWrite = [&](int byteCount) -> bool
        {
            (void)byteCount;
            return true;
        };
        auto emitByte = [&](std::uint8_t value) -> bool
        {
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd)] = value;
            ++m_physical.bytecodeEnd;
            return true;
        };
        auto emitIntObject = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_physical.bytecodeEnd);
            host().m_bytecode[off] = script::opcodeValue(script::VmOpcode::PushInteger);
            std::memcpy(host().m_bytecode.data() + off + 1, &value, sizeof(value));
            m_physical.bytecodeEnd += 5;
            return true;
        };
        auto emitIntPayload = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_physical.bytecodeEnd);
            std::memcpy(host().m_bytecode.data() + off, &value, sizeof(value));
            m_physical.bytecodeEnd += 4;
            return true;
        };
        auto emitByteAndIntPayload = [&](std::uint8_t opcode, int value) -> bool
        {
            if (!emitByte(opcode))
                return false;
            return emitIntPayload(value);
        };
        auto emitTrailingUnary = [&]() -> bool
        {
            if (unaryOpcode == 0)
                return true;
            return emitByte(static_cast<std::uint8_t>(unaryOpcode));
        };

        
        const unsigned char current = sourceByteAtCursor();
        if (std::isdigit(current))
        {
            STRING token;
            readIdentifier(token);
            int value = 0;
            std::sscanf(token.c_str(), "%i", &value);
            if (unaryOpcode == script::opcodeValue(script::VmOpcode::Negate))
            {
                value = -value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
            {
                value = ~value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::LogicalNot))
            {
                value = value ? 0 : 1;
                unaryOpcode = 0;
            }
            if (!emitIntObject(value))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '"')
        {
            if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                return 1;
            const std::size_t outOff = static_cast<std::size_t>(m_physical.bytecodeEnd);
            const int written = readQuotedStringLiteral(reinterpret_cast<char*>(host().m_bytecode.data() + outOff));
            m_physical.bytecodeEnd += written;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '\'')
        {
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
            const signed char ch = static_cast<signed char>(sourceByteAtCursor());
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
            if (!emitIntObject(static_cast<int>(ch)))
                return 1;
            if (sourceByteAtCursor() != '\'')
            {
                reportCompileError(13, "second '", 0);
                std::exit(1);
            }
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("sizeof"))
        {
            if (!matchToken("("))
            {
                reportCompileError(13, "'(' for sizeof", 0);
                std::exit(1);
            }

            if (!emitByte(script::opcodeValue(script::VmOpcode::PushInteger)))
                return 1;

            int sizeofValue = 4;
            if (!matchToken("int") && !matchToken("string"))
            {
                STRING sizeofName;
                readIdentifier(sizeofName);
                int foundIndex = -1;
                const int count = functionCount();
                const auto& records = host().m_functionTable.items();
                for (int i = count - 1; i >= 0; --i)
                {
                    if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), sizeofName.c_str()) == 0)
                    {
                        foundIndex = i;
                        break;
                    }
                }
                if (foundIndex < 0 || records[static_cast<std::size_t>(foundIndex)].flags != 1)
                {
                    reportCompileError(4, "sizeof parameter", 0);
                    std::exit(1);
                }
                sizeofValue = records[static_cast<std::size_t>(foundIndex)].value2 << 2;
            }

            if (!emitIntPayload(sizeofValue))
                return 1;
            requireToken(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("static"))
        {
            if (matchToken("int"))
            {
                do
                {
                    compileIntDeclaration();
                }
                while (matchToken(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            if (matchToken("string"))
            {
                do
                {
                    compileStringDeclaration();
                }
                while (matchToken(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (matchToken("int"))
        {
            do
            {
                compileIntDeclaration();
            }
            while (matchToken(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("string"))
        {
            do
            {
                compileStringDeclaration();
            }
            while (matchToken(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("return"))
        {
            compileExpression();
            if (!emitByte(script::opcodeValue(script::VmOpcode::Return)))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("("))
        {
            compileExpression();
            requireToken(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (std::isalpha(current))
        {
            auto sourceCursorChar = [&]() -> int
            {
                if (host().m_portableSourceCursorOffset < 0 ||
                    static_cast<std::size_t>(host().m_portableSourceCursorOffset) >= host().m_sourceBuffer.size())
                    return -1;
                return sourceByteAtCursor();
            };
            auto emitFormattedPrimaryDiagnostic = [&](int code, const char* fmt, const STRING& name)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), fmt, name.c_str());
                reportCompileError(code, buffer, 0);
                std::exit(1);
            };

            STRING name;
            readIdentifier(name);

            const int foundIndex = host().m_functionTable.findLastByName(name);
            if (foundIndex < 0)
            {
                if (sourceCursorChar() == ':')
                {
                    setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                    host().m_functionTable.appendLabelDefinition(name, m_physical.bytecodeEnd);
                    syncPhysicalFunctionList();
                    return compilePrimaryExpression();
                }
                emitFormattedPrimaryDiagnostic(10, "Undeclared identifier\t'%s'", name);
            }

            const auto& records = host().m_functionTable.items();
            const std::uint8_t flags = records[static_cast<std::size_t>(foundIndex)].flags;
            if (flags == 8)
            {
                if (sourceCursorChar() != ':')
                    emitFormattedPrimaryDiagnostic(10, "Incorrect use\tlabel '%s'", name);
                setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
                int patchOffset = 0;
                if (host().m_functionTable.resolvePendingLabel(foundIndex, m_physical.bytecodeEnd, patchOffset))
                {
                    const int delta = m_physical.bytecodeEnd - patchOffset;
                    std::memcpy(host().m_bytecode.data() + patchOffset, &delta, sizeof(delta));
                }
                return compilePrimaryExpression();
            }

            if (flags == 7)
            {
                if (sourceCursorChar() == ':')
                    emitFormattedPrimaryDiagnostic(10, "Label\tredefinition '%s'", name);
                emitFormattedPrimaryDiagnostic(10, "Incorrect use\tlabel '%s'", name);
            }

            if (flags == 2)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].value1;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].value2;
                const int externOpcode = records[static_cast<std::size_t>(foundIndex)].value0;

                requireToken("(");
                int parsedCount = 0;
                if (!matchToken(")"))
                {
                    for (;;)
                    {
                        compileExpression();
                        matchToken(",");
                        ++parsedCount;
                        if (matchToken(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;
                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::ReadVariable), parameterBase + parsedCount))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "extern function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByte(static_cast<std::uint8_t>(externOpcode)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 3)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].value1;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].value2;
                const int functionOffset = records[static_cast<std::size_t>(foundIndex)].value0;

                requireToken("(");
                int parsedCount = 0;
                if (!matchToken(")"))
                {
                    for (;;)
                    {
                        compileExpression();
                        matchToken(",");
                        if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                            return 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                            return 1;
                        ++parsedCount;
                        if (matchToken(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;

                    if ((defaultRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        const std::size_t textLen = std::strlen(defaultRecord->text.c_str()) + 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                            return 1;
                        if (!canWrite(static_cast<int>(textLen)))
                            return 1;
                        std::memcpy(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                            defaultRecord->text.c_str(), textLen);
                        m_physical.bytecodeEnd += static_cast<int>(textLen);
                    }
                    else
                    {
                        if (!emitIntObject(defaultRecord->intValue))
                            return 1;
                    }

                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                        return 1;
                    if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::CallScriptFunction), functionOffset))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 4)
            {
                const STRING& textValue = records[static_cast<std::size_t>(foundIndex)].text;
                const std::size_t textLen = std::strlen(textValue.c_str()) + 1;
                if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                    return 1;
                if (!canWrite(static_cast<int>(textLen)))
                    return 1;
                std::memcpy(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                    textValue.c_str(), textLen);
                m_physical.bytecodeEnd += static_cast<int>(textLen);
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 5)
            {
                const int value = records[static_cast<std::size_t>(foundIndex)].value0;
                if (!emitIntObject(value))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 1)
            {
                const int stackIndex = records[static_cast<std::size_t>(foundIndex)].value0;
                const script::StackObject* stackRecord = mutableExecutionStackStorageAt(stackIndex);

                int indexBytecodeStart = 0;
                int savedIndexBytecodeSize = 0;
                std::uint8_t* savedIndexBytecode = nullptr;
                if (matchToken("["))
                {
                    indexBytecodeStart = m_physical.bytecodeEnd;
                    if ((stackRecord->flags & (script::STACK_OBJECT_STRING |
                                               script::STACK_OBJECT_ARRAY |
                                               script::STACK_OBJECT_DYNAMIC)) == 0)
                    {
                        reportCompileError(10, "[] for not array", 0);
                        std::exit(1);
                    }

                    compileExpression();
                    if (!emitByte(script::opcodeValue(script::VmOpcode::ArrayIndex)))
                        return 1;
                    savedIndexBytecodeSize = m_physical.bytecodeEnd - indexBytecodeStart;
                    savedIndexBytecode = static_cast<std::uint8_t*>(
                        ::operator new(static_cast<std::size_t>(savedIndexBytecodeSize)));
                    std::memcpy(savedIndexBytecode,
                        host().m_bytecode.data() + static_cast<std::size_t>(indexBytecodeStart),
                        static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_physical.bytecodeEnd = indexBytecodeStart;
                    requireToken("]");
                }

                std::uint8_t assignmentOpcode = prefixOpcode;
                script::BinaryCommand compoundCommand = script::BinaryCommand::Add;
                auto sourceCharAtOffset = [&](int delta) -> int
                {
                    const int pos = host().m_portableSourceCursorOffset + delta;
                    return host().m_sourceBuffer[static_cast<std::size_t>(pos)];
                };

                if (sourceCharAtOffset(1) != '=' && matchToken("="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::Assign);
                }
                else if (matchToken("+="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Add;
                }
                else if (matchToken("-="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Subtract;
                }
                else if (matchToken("/="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Divide;
                }
                else if (matchToken("*="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Multiply;
                }
                else if (matchToken("%="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Modulo;
                }
                else if (matchToken("&="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseAnd;
                }
                else if (matchToken("|="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseOr;
                }
                else if (matchToken("^="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseXor;
                }
                else if (matchToken("<<="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftLeft;
                }
                else if (matchToken(">>="))
                {
                    compileExpression();
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftRight;
                }
                else if (matchToken("++"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostIncrement);
                }
                else if (matchToken("--"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostDecrement);
                }

                if (savedIndexBytecode)
                {
                    std::memcpy(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                        savedIndexBytecode, static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_physical.bytecodeEnd += savedIndexBytecodeSize;
                    ::operator delete(savedIndexBytecode);
                    savedIndexBytecode = nullptr;
                }
                else if ((stackRecord->flags & (script::STACK_OBJECT_ARRAY | script::STACK_OBJECT_DYNAMIC)) != 0)
                {
                    if (assignmentOpcode == script::opcodeValue(script::VmOpcode::PostIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PostDecrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreDecrement))
                    {
                        reportCompileError(10, "Increment or decrement for array", 0);
                        std::exit(1);
                    }
                    if (assignmentOpcode != script::opcodeValue(script::VmOpcode::ReadVariable))
                    {
                        reportCompileError(4, "operation for array", assignmentOpcode);
                        std::exit(1);
                    }
                }

                if (!emitByteAndIntPayload(static_cast<std::uint8_t>(assignmentOpcode), stackIndex))
                    return 1;
                if (assignmentOpcode == script::opcodeValue(script::VmOpcode::CompoundAssign) && !emitByte(script::opcodeValue(compoundCommand)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            return emitTrailingUnary() ? 0 : 1;
        }

        if (unaryOpcode != 0)
        {
            reportCompileError(10, "error symbol", 0);
            std::exit(1);
        }
        return 0;
    }

    void SCRIPT::emitOrFoldBinaryCommand(int bytecodeStart, int opcode)
    {
        const std::size_t start = static_cast<std::size_t>(bytecodeStart);
        const bool canReadTwoConstants =
            host().m_bytecode[start] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            host().m_bytecode[start + 5] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            m_physical.bytecodeEnd - bytecodeStart == 10;

        if (canReadTwoConstants)
        {
            int lhs = 0;
            int rhs = 0;
            std::memcpy(&lhs, host().m_bytecode.data() + start + 1, sizeof(lhs));
            std::memcpy(&rhs, host().m_bytecode.data() + start + 6, sizeof(rhs));

            bool hasFoldedValue = true;
            int folded = lhs;
            switch (static_cast<script::BinaryCommand>(opcode))
            {
            case script::BinaryCommand::Multiply:
                folded = lhs * rhs;
                break;
            case script::BinaryCommand::Divide:
                folded = lhs / rhs;
                break;
            case script::BinaryCommand::Modulo:
                folded = lhs % rhs;
                break;
            case script::BinaryCommand::Add:
                folded = lhs + rhs;
                break;
            case script::BinaryCommand::Subtract:
                folded = lhs - rhs;
                break;
            case script::BinaryCommand::ShiftRight:
                folded = lhs >> (rhs & 31);
                break;
            case script::BinaryCommand::ShiftLeft:
                folded = lhs << (rhs & 31);
                break;
            case script::BinaryCommand::BitwiseXor:
                folded = lhs ^ rhs;
                break;
            case script::BinaryCommand::BitwiseAnd:
                folded = lhs & rhs;
                break;
            case script::BinaryCommand::BitwiseOr:
                folded = lhs | rhs;
                break;
            default:
                hasFoldedValue = false;
                break;
            }

            if (hasFoldedValue)
                std::memcpy(host().m_bytecode.data() + start + 1, &folded, sizeof(folded));
            m_physical.bytecodeEnd -= 5;
            return;
        }

        const std::size_t writeOffset = static_cast<std::size_t>(m_physical.bytecodeEnd);
        host().m_bytecode[writeOffset] = static_cast<std::uint8_t>(opcode);
        ++m_physical.bytecodeEnd;
    }



    void SCRIPT::compileMultiplicativeExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compilePrimaryExpression();
        for (;;)
        {
            if (matchToken("*"))
            {
                compilePrimaryExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Multiply));
                continue;
            }
            if (matchToken("/"))
            {
                compilePrimaryExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Divide));
                continue;
            }
            if (matchToken("%"))
            {
                compilePrimaryExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Modulo));
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileAdditiveExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileMultiplicativeExpression();
        for (;;)
        {
            if (matchToken("+"))
            {
                compileMultiplicativeExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Add));
                continue;
            }
            if (matchToken("-"))
            {
                compileMultiplicativeExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::Subtract));
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileComparisonExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileAdditiveExpression();
        for (;;)
        {
            if (matchToken(">="))
            {
                compileAdditiveExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::GreaterEqual);
                continue;
            }
            if (matchToken(">>"))
            {
                compileAdditiveExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftRight));
                continue;
            }
            if (matchToken(">"))
            {
                compileAdditiveExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Greater);
                continue;
            }
            if (matchToken("<="))
            {
                compileAdditiveExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LessEqual);
                continue;
            }
            if (matchToken("<<"))
            {
                compileAdditiveExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftLeft));
                continue;
            }
            if (matchToken("<"))
            {
                compileAdditiveExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Less);
                continue;
            }
            if (matchToken("=="))
            {
                compileAdditiveExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Equal);
                continue;
            }
            if (matchToken("!="))
            {
                compileAdditiveExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::NotEqual);
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileExpression()
    {
        const int bytecodeStart = m_physical.bytecodeEnd;
        compileComparisonExpression();
        for (;;)
        {
            if (matchToken("^"))
            {
                compileComparisonExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseXor));
                continue;
            }
            if (matchToken("&&"))
            {
                compileComparisonExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LogicalAnd);
                continue;
            }
            if (matchToken("&"))
            {
                compileComparisonExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseAnd));
                continue;
            }
            if (matchToken("||"))
            {
                compileComparisonExpression();
                host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LogicalOr);
                continue;
            }
            if (matchToken("|"))
            {
                compileComparisonExpression();
                emitOrFoldBinaryCommand(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseOr));
                continue;
            }
            break;
        }
    }

    void SCRIPT::compileExpressionList()
    {
        for (;;)
        {
            compileExpression();
            const std::size_t writeOffset = static_cast<std::size_t>(m_physical.bytecodeEnd);
            host().m_bytecode[writeOffset] = script::opcodeValue(script::VmOpcode::StatementEnd);
            ++m_physical.bytecodeEnd;
            std::memcpy(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd),
                &m_physical.sourceLine, sizeof(m_physical.sourceLine));
            m_physical.bytecodeEnd += 4;
            if (!matchToken(","))
                break;
        }
    }

    void SCRIPT::compileStatement(std::int32_t* breakPatchList)
    {
        const int iffMatched = matchToken("iff");
        if (iffMatched || matchToken("if"))
        {
            requireToken("(");
            compileExpression();
            requireToken(")");

            const script::VmOpcode branchOpcode = iffMatched
                ? script::VmOpcode::IfFalseChain
                : script::VmOpcode::If;
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(branchOpcode);
            const int firstPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            compileStatement(breakPatchList);
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(firstPatchOffset)) = (m_physical.bytecodeEnd - firstPatchOffset);

            if (!matchToken("else"))
                return;

            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(firstPatchOffset)) = (m_physical.bytecodeEnd - firstPatchOffset + 5);
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int elsePatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            compileStatement(breakPatchList);
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(elsePatchOffset)) = (m_physical.bytecodeEnd - elsePatchOffset);
            return;
        }

        if (matchToken("while"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            requireToken("(");
            const int loopConditionStart = m_physical.bytecodeEnd;
            compileExpression();
            requireToken(")");
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            compileStatement(localBreakPatchList);
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int jumpBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (loopConditionStart - jumpBackPatchOffset);
            m_physical.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(conditionPatchOffset)) = (m_physical.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(patchOffset)) = (m_physical.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("do"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            const int loopBodyStart = m_physical.bytecodeEnd;
            compileStatement(localBreakPatchList);

            requireToken("while");
            requireToken("(");
            compileExpression();
            requireToken(")");
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::LogicalNot);
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int branchBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (loopBodyStart - branchBackPatchOffset);
            m_physical.bytecodeEnd += 4;

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(patchOffset)) = (m_physical.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("for"))
        {
            std::int32_t localBreakPatchList[0x80] = {};

            requireToken("(");
            compileExpressionList();
            requireToken(";");

            const int conditionStart = m_physical.bytecodeEnd;
            compileExpression();
            requireToken(";");
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int skipUpdatePatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;

            const int updateStart = m_physical.bytecodeEnd;
            compileExpressionList();
            requireToken(")");
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int updateJumpBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (conditionStart - updateJumpBackPatchOffset);
            m_physical.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(skipUpdatePatchOffset)) = (m_physical.bytecodeEnd - skipUpdatePatchOffset);

            compileStatement(localBreakPatchList);
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int bodyJumpBackPatchOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (updateStart - bodyJumpBackPatchOffset);
            m_physical.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(conditionPatchOffset)) = (m_physical.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(patchOffset)) = (m_physical.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("break"))
        {
            requireToken(";");
            if (!breakPatchList)
            {
                reportCompileError(10, "'break' without loop", 0);
                std::exit(1);
            }

            int slot = 0;
            while (slot < 0x80 && breakPatchList[slot] != 0)
                ++slot;
            if (slot >= 0x80)
            {
                reportCompileError(10, "Too many 'break'", 0);
                std::exit(1);
            }
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            breakPatchList[slot] = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (0);
            m_physical.bytecodeEnd += 4;
            breakPatchList[slot + 1] = 0;
            return;
        }

        if (matchToken("goto"))
        {
            STRING name;
            readIdentifier(name);

            int labelIndex = host().m_functionTable.findLastByName(name);
            const bool appendedPendingLabel = labelIndex < 0;
            if (appendedPendingLabel)
            {
                appendFunctionRecord(name, 8, STRING(), m_physical.bytecodeEnd + 1, 0, 0);
                labelIndex = functionCount() - 1;
            }

            const auto& records = host().m_functionTable.items();
            const script::LogicFunctionRecord& labelRecord = records[static_cast<std::size_t>(labelIndex)];

            if (labelRecord.flags == 8 && !appendedPendingLabel)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "second use undefined label '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            if (labelRecord.flags != 7 && labelRecord.flags != 8)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "'%s' is not label", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int payloadOffset = m_physical.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(host().m_bytecode.data() + static_cast<std::size_t>(m_physical.bytecodeEnd)) = (labelRecord.value0 - payloadOffset);
            m_physical.bytecodeEnd += 4;
            return;
        }

        if (matchToken("{"))
        {
            const int savedFunctionCount = functionCount();
            while (!matchToken("}"))
                compileStatement(breakPatchList);
            host().m_functionTable.truncateCount(savedFunctionCount);
            syncPhysicalFunctionList();
            return;
        }

        compileExpressionList();
        requireToken(";");
    }

    int SCRIPT::compileNextSourceItem()
    {
        if (skipTriviaAndPreprocess())
            return 1;

        const std::size_t cursor = static_cast<std::size_t>(host().m_portableSourceCursorOffset);
        auto starts = [&](const char* token) -> bool
        {
            return std::strncmp(reinterpret_cast<const char*>(host().m_sourceBuffer.data() + cursor),
                token, std::strlen(token)) == 0;
        };

        if (starts("#define"))
            return compileDefineDirective();
        if (starts("#undef"))
            return compileUndefDirective();
        // Unlike #define/#undef, retail routes #include/extern through
        // matchToken, which enforces identifier-boundary semantics.  A raw
        // prefix such as "externX" must fall through to function parsing.
        if (matchToken("#include"))
            return compileIncludeDirective();
        if (matchToken("extern"))
            return compileExternDirective();

        if (matchToken("static"))
        {
            if (matchToken("int"))
            {
                do
                {
                    compileIntDeclaration();
                }
                while (matchToken(","));
                requireToken(";");
                return skipTriviaAndPreprocess();
            }
            if (matchToken("string"))
            {
                do
                {
                    compileStringDeclaration();
                }
                while (matchToken(","));
                requireToken(";");
                return skipTriviaAndPreprocess();
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (matchToken("int"))
        {
            do
            {
                compileIntDeclaration();
            }
            while (matchToken(","));
            requireToken(";");
            return skipTriviaAndPreprocess();
        }
        if (matchToken("string"))
        {
            do
            {
                compileStringDeclaration();
            }
            while (matchToken(","));
            requireToken(";");
            return skipTriviaAndPreprocess();
        }

        return compileFunctionDirective();
    }

    int SCRIPT::compileDefineDirective()
    {
        setSourceCursorOffset(host().m_portableSourceCursorOffset + 7);
        STRING name;
        m_physical.parseMode = 1;
        readIdentifier(name);
        m_physical.parseMode = 0;

        STRING value;
        readSourceLine(value);

        addOrReplaceDefine(name, value);

        // Retail destroys the temporary macro value before LABEL_148 calls
        // skipTriviaAndPreprocess; the macro name temporary survives until after that call.
        destroyStringStorage(value);
        value.ResetSharedEmptyWithoutRelease();
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileUndefDirective()
    {
        setSourceCursorOffset(host().m_portableSourceCursorOffset + 6);
        STRING name;
        m_physical.parseMode = 1;
        readIdentifier(name);
        m_physical.parseMode = 0;

        if (undefine(name) < 0)
        {
            reportCompileError(4, "#undef parameters", 0);
            std::exit(1);
        }
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileIncludeDirective()
    {
        STRING savedScriptFile = host().m_scriptFile;

        const unsigned char delimiter = sourceByteAtCursor();
        if (delimiter != '"' && delimiter != '<')
        {
            reportCompileError(13, "include file name", 0);
            std::exit(1);
        }

        setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);

        // Retail uses a fixed 0x400-byte stack buffer and does not add a
        // protective filename-length check.  Preserve that owner shape rather
        // than substituting a heap std::string.
        char includeName[0x400];
        int includeNameLength = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '"' || c == '>')
                break;
            if (host().m_portableSourceCursorOffset >= host().m_portableSourceEndOffset)
            {
                reportCompileError(10, "End of file", 0);
                std::exit(1);
            }
            includeName[includeNameLength++] = static_cast<char>(c);
            setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);
        }
        includeName[includeNameLength] = '\0';
        setSourceCursorOffset(host().m_portableSourceCursorOffset + 1);

        FileStream includeStream(includeName, "rb");
        if (!includeStream.isOpen())
        {
            reportCompileError(7, includeName, 0);
            std::exit(1);
        }

        const std::uint32_t includeLength = retailFileLength32(includeStream);
        const int savedCursor = host().m_portableSourceCursorOffset;
        const int savedEnd = host().m_portableSourceEndOffset;
        const int savedLine = m_physical.sourceLine;
        const int savedConditionalDepth = m_physical.conditionalDepth;

        // Retail allocates the include owner while the parent allocation is
        // still live.  Keep host() on the parent during the allocation-failure
        // diagnostic path, then transfer ownership only after allocation
        // succeeds.
        script::RetailByteBuffer includeSourceOwner;
        try
        {
            const std::uint32_t includeAllocationSize = includeLength + static_cast<std::uint32_t>(SourceBufferPadding);
            includeSourceOwner.resize(static_cast<std::size_t>(includeAllocationSize));
        }
        catch (...)
        {
            reportCompileError(2, "include", 0);
            std::exit(1);
        }

        script::RetailByteBuffer savedSourceOwner;
        savedSourceOwner.swap(host().m_sourceBuffer);
        host().m_sourceBuffer.swap(includeSourceOwner);

        host().m_portableSourceCursorOffset = static_cast<int>(SourcePayloadOffset);
        const std::uint32_t includeEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + includeLength;
        host().m_portableSourceEndOffset = static_cast<std::int32_t>(includeEndOffset);
        syncPhysicalSourcePointers();
        if (includeLength != 0)
        {
            includeStream.read(host().m_sourceBuffer.data() + SourcePayloadOffset,
                includeLength);
        }

        m_physical.sourceLine = 0;
        assignStringFromCString(host().m_scriptFile, includeName);
        m_physical.scriptFileToken = retailPointerToken(host().m_scriptFile.c_str());
        m_physical.conditionalDepth = 0;

        int status = compileNextSourceItem();
        while (status == 0)
            status = compileNextSourceItem();

        includeStream.close();

        // Free the include allocation first, then put back the untouched parent
        // allocation and exact cursor/end offsets.
        host().m_sourceBuffer.clear();
        script::RetailByteBuffer().swap(host().m_sourceBuffer);
        host().m_sourceBuffer.swap(savedSourceOwner);
        host().m_portableSourceCursorOffset = savedCursor;
        host().m_portableSourceEndOffset = savedEnd;
        syncPhysicalSourcePointers();
        m_physical.sourceLine = savedLine;
        m_physical.conditionalDepth = savedConditionalDepth;
        assignStringFromString(host().m_scriptFile, savedScriptFile);
        m_physical.scriptFileToken = retailPointerToken(host().m_scriptFile.c_str());

        // Retail releases the saved filename temporary before the shared
        // LABEL_148 whitespace scan.
        destroyStringStorage(savedScriptFile);
        savedScriptFile.ResetSharedEmptyWithoutRelease();
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileExternDirective()
    {
        const int savedFunctionCount = functionCount();

        STRING name;
        readIdentifier(name);

        const auto& records = host().m_functionTable.items();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
        requireToken("(");
        for (;;)
        {
            if (matchToken("int"))
            {
                compileIntDeclaration();
            }
            else if (matchToken("string"))
            {
                compileStringDeclaration();
            }

            if (!matchToken(","))
                break;
        }
        requireToken(")");

        const int parameterCount = executionStackCount() - stackBase;
        const int nativeCode = parseConstantIntExpression();

        const unsigned char previous = host().m_sourceBuffer[static_cast<std::size_t>(host().m_portableSourceCursorOffset - 1)];
        if (!std::isdigit(retailCtypeInput41BC50(previous)))
        {
            reportCompileError(13, "extern function code", 0);
            std::exit(1);
        }

        host().m_functionTable.truncateCount(savedFunctionCount);
        syncPhysicalFunctionList();
        appendFunctionRecord(name, 2, STRING(), nativeCode, stackBase, parameterCount);
        requireToken(";");
        return skipTriviaAndPreprocess();
    }


    int SCRIPT::compileFunctionDirective()
    {
        const int savedFunctionCount = functionCount();
        STRING name;
        readIdentifier(name);

        const auto& records = host().m_functionTable.items();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
        const int bytecodeStart = m_physical.bytecodeEnd;

        requireToken("(");
        for (;;)
        {
            if (matchToken("int"))
            {
                compileIntDeclaration();
            }
            else if (matchToken("string"))
            {
                compileStringDeclaration();
            }

            if (!matchToken(","))
                break;
        }
        requireToken(")");

        const int parameterCount = executionStackCount() - stackBase;
        requireToken("{");
        while (!matchToken("}"))
            compileStatement(nullptr);

        host().m_bytecode[static_cast<std::size_t>(m_physical.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Return);

        host().m_functionTable.truncateCount(savedFunctionCount);
        syncPhysicalFunctionList();
        appendFunctionRecord(name, 3, STRING(), bytecodeStart, stackBase, parameterCount);
        if (std::strcmp(name.c_str(), "main") == 0)
            m_physical.fallbackFunction = functionCount() - 1;

        return skipTriviaAndPreprocess();
    }


    namespace
    {
        bool readVmDword(const script::RetailByteBuffer& bytecode, int offset, int& value)
        {
            // Retail reads the dword directly from [SCRIPT+0x34]+offset.
            std::uint32_t raw = 0;
            std::memcpy(&raw, bytecode.data() + static_cast<std::size_t>(offset), sizeof(raw));
            value = static_cast<int>(raw);
            return true;
        }

        int stackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? script::ParseStackIntegerText(value.text.c_str())
                : value.intValue;
        }

        int stackObjectNumeric41F2D0(const script::StackObject& value)
        {
            return stackValueToInteger(value);
        }


        int scriptSpritePointerValue(SPRITE* sprite) noexcept
        {
            // Retail SCRIPT unit/reference values are raw Win32 SPRITE* values
            // stored in the +4 dword of the 12-byte stack object.
            return sprite
                ? static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sprite)))
                : 0;
        }

        SPRITE* scriptResolveSpriteReference(int value) noexcept
        {
            if (value == 0)
                return nullptr;
            return reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        bool isValidNvid(int nvid) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            return nvid >= 0 && nvid < table.count() &&
                   table.slot(nvid) != nullptr;
        }

        VID* resolveVidByNvid(int nvid) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            if (nvid >= 0 && nvid < table.count())
            {
                if (VID* const vid = table.slot(nvid))
                    return vid;
            }
            return MAP::NullVid();
        }

        PLAYER* scriptPlayerSlot(int index) noexcept
        {
#ifdef _WIN32
            return win::applicationWinInstance()->playerSlotByIndex(index);
#else
            (void)index;
            return nullptr;
#endif
        }

        SPRITE* beginReverseDrawPassIteration(core::ApplicationDrawDispatcherState& drawState, int pass, int* cursor)
        {
            // Retail beginReverseDrawPassIteration: initialize the reverse Application draw-bucket
            // iterator at count-1, skip sparse null cells, and return the first
            // live SPRITE without pre-decrementing the cursor.
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            *cursor = bucket.count() - 1;
            while (*cursor >= 0 && !bucket.spriteAt(*cursor))
                --(*cursor);
            return *cursor >= 0 ? bucket.spriteAt(*cursor) : nullptr;
        }

        int g_unitIteratorArmyBucket = 0;
        int g_unitIteratorOrdinal = 0;

        SPRITE* findRootUnitByArmyOrdinal(int a1, int a2, int* outOrdinal)
        {
            SPRITE_COLLECTOR_HASH_MAP* const map = GlobalSpriteHashMap();
            int skipped = 0;
            int matched = 0;
            int count = map->overflowCount();
            if (!count)
            {
                *outOrdinal = 0;
                return nullptr;
            }

            int index = count - 1;
            SPRITE* sprite = map->overflowSpriteAt(index);
            if (!sprite)
            {
                *outOrdinal = 0;
                return nullptr;
            }

            for (;;)
            {
                if (sprite->Vid()->spriteClassId() == 21 && !sprite->engineChainPrevious())
                {
                    if (a1 == 4 || sprite->armyIndex() == a1)
                    {
                        ++skipped;
                        if (++matched == a2)
                        {
                            *outOrdinal = skipped;
                            SPRITE* const special = static_cast<ENGINE*>(sprite)->findEngineChainSpecialWeaponNode();
                            if (!special)
                                return sprite;
                            if (((sprite->runtimeFlags() ^ special->runtimeFlags()) >> 8) & 0x0Cu)
                                --matched;
                            else
                                return special;
                        }
                    }
                    else
                    {
                        ++skipped;
                    }
                }

                if (index > map->overflowCount())
                    index = map->overflowCount();
                --index;
                if (index < 0)
                {
                    *outOrdinal = 0;
                    return nullptr;
                }
                sprite = map->overflowSpriteAt(index);
                if (!sprite)
                {
                    *outOrdinal = 0;
                    return nullptr;
                }
            }
        }

        SPRITE* beginRootUnitArmyIteration(int a1)
        {
            if (a1 < 0)
                return nullptr;
            int bucket = a1;
            if (bucket >= 4)
                bucket = 3;
            g_unitIteratorArmyBucket = bucket;
            g_unitIteratorOrdinal = 1;
            return findRootUnitByArmyOrdinal(bucket, 1, &a1);
        }

        SPRITE* continueRootUnitArmyIteration()
        {
            int value = 0;
            return findRootUnitByArmyOrdinal(g_unitIteratorArmyBucket, ++g_unitIteratorOrdinal, &value);
        }

        
        constexpr std::uint32_t scriptSinTableBits[256] =
        {
        0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
        0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
        0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
        0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
        0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
        0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
        0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
        0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
        0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
        0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
        0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
        0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
        0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
        0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
        0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
        0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
        0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
        0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
        0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
        0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
        0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
        0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
        0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
        0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
        0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
        0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
        0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
        0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
        0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
        0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
        0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
        };

        constexpr std::uint32_t scriptCosTableBits[256] =
        {
        0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
        0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
        0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
        0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
        0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
        0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
        0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
        0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
        0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
        0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
        0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
        0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
        0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
        0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
        0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
        0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
        0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
        0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
        0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
        0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
        0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
        0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
        0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
        0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
        0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
        0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
        0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
        0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
        0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
        0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
        0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
        0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        };

        float scriptNativeFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        int scriptNativeTable1024ToInt(float value)
        {
            return static_cast<int>(value * 1024.0f);
        }

        int scriptNativeSin1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::rawDirectionSin(angle));
        }

        int scriptNativeCos1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::rawDirectionCos(angle));
        }
    }

    STRING SCRIPT::getVariableString(const STRING& expression)
    {
        // Variable-string lookup preserves the retail stack coercion order.
        // The input is split on the first '['.  The left token is searched
        // backwards in the 24-byte SCRIPT function/variable table.  The
        // right token is parsed with the original integer parser and added to
        // record +0x0C (value0), producing the raw 12-byte stack-record index.
        // Integer records are converted to decimal text; string records return
        // their +0x08 AS_STRING contents.  Missing variables log the original
        // diagnostic and return the shared empty string.
        STRING variableName;
        constructLeftOfFirstMarker(expression, variableName, "[");
        const int functionIndex = host().m_functionTable.findLastByName(variableName);
        if (functionIndex < 0)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Can't find variable '%s' in GetVariableString", expression.c_str());
            return STRING();
        }

        STRING indexText;
        constructRightOfFirstMarker(expression, indexText, "[");
        const int elementIndex = script::ParseStackIntegerText(indexText.c_str());
        const auto& records = host().m_functionTable.items();
        const int stackIndex = records[static_cast<std::size_t>(functionIndex)].value0 + elementIndex;
        script::StackObject* value = mutableExecutionStackStorageAt(stackIndex);

        if ((value->flags & script::STACK_OBJECT_INT) != 0)
        {
            // Retail copies the decimal conversion into the record's +0x08
            // AS_STRING slot before returning a deep copy of that text.
            value->text = script::IntToStackString(value->intValue);
        }
        return STRING(value->text.c_str());
    }

    int SCRIPT::callFunction(int functionIndex, int arg3, int arg4)
    {
        if (host().m_bytecode.empty())
            return 0;

        int resolvedFunction = functionIndex;
        if (resolvedFunction < 0)
            resolvedFunction = m_physical.fallbackFunction;

        if (resolvedFunction < 0 || resolvedFunction >= m_physical.functionCount)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Call unexisted function %i", resolvedFunction);
            return 0;
        }

        const script::LogicFunctionRecord& fn = host().m_functionTable.items()[static_cast<std::size_t>(resolvedFunction)];
        if (fn.flags != 3)
        {
            LOG::Write("!!!ERROR!!!LOGIC: Call unexisted function %s()", fn.name.c_str());
            return 0;
        }

        const int savedStackCount = m_physical.stackCount;
        const int bytecodeLimit = m_physical.bytecodeEnd;
        int cursor = fn.value0;
        int controlAnchor = cursor;
        int result = 0;

        script::StackObject savedCountObject;
        savedCountObject.assignInt(savedStackCount);
        appendExecutionStackObject(savedCountObject);

        script::StackObject returnObject;
        returnObject.assignInt(bytecodeLimit);
        appendExecutionStackObject(returnObject);
        int frameBase = static_cast<int>(
            static_cast<std::uint32_t>(savedStackCount) + 2u);

        if (fn.value2 >= 1)
        {
            script::StackObject* arg = mutableExecutionStackStorageAt(fn.value1);
            arg->assignFields(static_cast<std::uint8_t>(arg3 ? (script::STACK_OBJECT_INT | script::STACK_OBJECT_REF) : script::STACK_OBJECT_INT), arg3, STRING());
        }
        if (fn.value2 >= 2)
        {
            script::StackObject* arg = mutableExecutionStackStorageAt(fn.value1 + 1);
            arg->assignFields(static_cast<std::uint8_t>(arg4 ? (script::STACK_OBJECT_INT | script::STACK_OBJECT_REF) : script::STACK_OBJECT_INT), arg4, STRING());
        }

        auto restoreStack = [&]()
        {
            m_physical.stackCount = savedStackCount;

            const int oldCapacity = m_physical.stackCapacity;
            if (savedStackCount <= oldCapacity)
                return;

            script::RetailRawArray<script::StackObject> replacement;
            try
            {
                replacement.resize(static_cast<std::size_t>(savedStackCount));
            }
            catch (...)
            {
                fatalLogError(g_fileLogger,
                            "!!!ERROR!!!::LIST: Not enough memory %i",
                            savedStackCount);
            }

            for (int i = 0; i < oldCapacity; ++i)
            {
                replacement[static_cast<std::size_t>(i)].copyStorageFrom(
                    host().m_executionStack[static_cast<std::size_t>(i)]);
            }

            host().m_executionStack.swap(replacement);
            m_physical.stackCapacity = savedStackCount;
            m_physical.stackTableToken = host().m_executionStack.empty()
                ? 0u : retailPointerToken(host().m_executionStack.data());
        };

        auto pushInt = [&](int value)
        {
            script::StackObject obj;
            obj.assignInt(value);
            appendExecutionStackObject(obj);
        };

        auto pushString = [&](const STRING& value)
        {
            script::StackObject obj;
            obj.assignString(value);
            appendExecutionStackObject(obj);
        };

        auto popObject = [&]() -> script::StackObject*
        {
            --m_physical.stackCount;
            return mutableExecutionStackStorageAt(m_physical.stackCount);
        };

        auto pushCopy = [&](const script::StackObject& value)
        {
            appendExecutionStackObject(value);
        };

        int arrayVmActive = 0;
        int arrayVmOffset = 0;

        auto clearArrayVmState = [&]()
        {
            arrayVmActive = 0;
            arrayVmOffset = 0;
        };

        while (cursor < bytecodeLimit)
        {
            if (m_physical.stackCount < frameBase)
            {
                LOG::Write("!!!ERROR!!!LOGIC: '%s' stack error %i", "pop, but not push", cursor);
                std::exit(1);
            }

            const std::uint8_t opcode = host().m_bytecode[static_cast<std::size_t>(cursor++)];
            const bool isBinaryCommand =
                opcode >= script::opcodeValue(script::BinaryCommand::Divide) &&
                opcode <= script::opcodeValue(script::BinaryCommand::ShiftLeft);
            const bool isVariableCommand =
                opcode >= script::opcodeValue(script::VmOpcode::PostIncrement) &&
                opcode <= script::opcodeValue(script::VmOpcode::CompoundAssign);
            if (isBinaryCommand || isVariableCommand)
            {
                if (isBinaryCommand)
                {
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                    script::StackObject* lhs = mutableExecutionStackStorageAt(m_physical.stackCount - 2);
                    lhs->applyBinaryCommand(opcode, *rhs);
                    --m_physical.stackCount;
                    continue;
                }

                int operandIndex = 0;
                readVmDword(host().m_bytecode, cursor, operandIndex);

                script::StackObject* operandRecord = mutableExecutionStackStorageAt(operandIndex);
                int targetIndex = operandIndex;
                if ((operandRecord->flags & script::STACK_OBJECT_DYNAMIC) != 0 && arrayVmActive != 0)
                    targetIndex = operandRecord->intValue;
                targetIndex += arrayVmOffset;

                script::StackObject* target = mutableExecutionStackStorageAt(targetIndex);

                const script::VmOpcode variableCommand = static_cast<script::VmOpcode>(opcode);
                auto mutateIncDec41F2D0 = [&](script::StackObject& value, int delta)
                {
                    value.flags = static_cast<std::uint8_t>(value.flags & ~script::STACK_OBJECT_REF);
                    if ((value.flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                        value.intValue += delta;
                };

                switch (variableCommand)
                {
                case script::VmOpcode::PostIncrement:
                    pushCopy(*target);
                    mutateIncDec41F2D0(*target, +1);
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                case script::VmOpcode::PostDecrement:
                    pushCopy(*target);
                    mutateIncDec41F2D0(*target, -1);
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                case script::VmOpcode::PreIncrement:
                    mutateIncDec41F2D0(*target, +1);
                    pushCopy(*target);
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                case script::VmOpcode::PreDecrement:
                    mutateIncDec41F2D0(*target, -1);
                    pushCopy(*target);
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                case script::VmOpcode::ReadVariable:
                    if (arrayVmActive == 0 && (target->flags & script::STACK_OBJECT_ARRAY) != 0)
                    {
                        script::StackObject temp;
                        temp.assignInt(operandIndex);
                        appendExecutionStackObject(temp);
                    }
                    else
                    {
                        pushCopy(*target);
                    }
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                case script::VmOpcode::AddressOf:
                {
                    script::StackObject temp;
                    if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        const auto rawAddress = reinterpret_cast<std::uintptr_t>(&target->text);
                        temp.assignInt(static_cast<int>(rawAddress & 0xFFFFFFFFu));
                    }
                    else
                    {
                        temp.assignInt(0);
                    }
                    appendExecutionStackObject(temp);
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                }
                case script::VmOpcode::Assign:
                {
                    script::StackObject* source = mutableExecutionStackStorageAt(m_physical.stackCount - 1);

                    script::StackObject* baseRecord = mutableExecutionStackStorageAt(operandIndex);
                    if ((baseRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        if (arrayVmActive != 0 && (baseRecord->flags & script::STACK_OBJECT_ARRAY) == 0)
                        {
                            std::string text = baseRecord->text.str();
                            const int numeric = stackObjectNumeric41F2D0(*source);
                            text[static_cast<std::size_t>(arrayVmOffset)] = static_cast<char>(numeric & 0xFF);
                            baseRecord->text.AssignBytes(text.data(), text.size());
                        }
                        else
                        {
                            if ((source->flags & script::STACK_OBJECT_INT) != 0)
                                target->text = script::IntToStackString(source->intValue);
                            else
                                target->text = source->text;
                        }
                    }
                    else
                    {
                        const int numeric = stackObjectNumeric41F2D0(*source);
                        target->flags = static_cast<std::uint8_t>(target->flags & ~static_cast<std::uint8_t>(script::STACK_OBJECT_REF | script::STACK_OBJECT_CHAR_WRITE));
                        target->intValue = numeric;
                        if ((source->flags & script::STACK_OBJECT_REF) != 0)
                            target->flags = static_cast<std::uint8_t>(target->flags | script::STACK_OBJECT_REF);
                    }
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                }
                case script::VmOpcode::CompoundAssign:
                {
                    --m_physical.stackCount;
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_physical.stackCount);
                    const std::uint8_t compoundOpcode = host().m_bytecode[static_cast<std::size_t>(cursor + 4)];
                    target->applyBinaryCommand(compoundOpcode, *rhs);
                    pushCopy(*target);
                    cursor += 5;
                    clearArrayVmState();
                    continue;
                }
                default:
                    cursor += 4;
                    clearArrayVmState();
                    continue;
                }
            }

            switch (static_cast<script::VmOpcode>(opcode))
            {
            case script::VmOpcode::PushInteger:
            {
                int value = 0;
                readVmDword(host().m_bytecode, cursor, value);
                pushInt(value);
                cursor += 4;
                break;
            }
            case script::VmOpcode::PushString:
            {
                const char* text = reinterpret_cast<const char*>(host().m_bytecode.data() + static_cast<std::size_t>(cursor));
                pushString(STRING(text));
                cursor += static_cast<int>(std::strlen(text)) + 1;
                break;
            }
            case script::VmOpcode::Negate:
            case script::VmOpcode::BitwiseNot:
            case script::VmOpcode::LogicalNot:
            {
                script::StackObject* top = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                const int value = stackObjectNumeric41F2D0(*top);
                top->flags = script::STACK_OBJECT_INT;
                if (opcode == script::opcodeValue(script::VmOpcode::Negate))
                    top->intValue = -value;
                else if (opcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
                    top->intValue = ~value;
                else
                    top->intValue = value == 0 ? 1 : 0;
                break;
            }
            case script::VmOpcode::If:
            {
                --m_physical.stackCount;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_physical.stackCount);
                const int condValue = stackObjectNumeric41F2D0(*cond);
                int payload = 0;
                readVmDword(host().m_bytecode, cursor, payload);
                cursor += condValue ? 4 : payload;
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!LOGIC: '%s' stack error %i", "if", cursor);
                controlAnchor = cursor;
                m_physical.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::StatementEnd:
            {
                cursor += 4;
                controlAnchor = cursor;
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!LOGIC: '%s' stack error %i", ";", cursor);
                m_physical.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::Pop:
                
                --m_physical.stackCount;
                break;
            case script::VmOpcode::Jump:
            {
                int payload = 0;
                readVmDword(host().m_bytecode, cursor, payload);
                cursor += payload;
                break;
            }
            case script::VmOpcode::IfFalseChain:
            {
                --m_physical.stackCount;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_physical.stackCount);
                const int condValue = stackObjectNumeric41F2D0(*cond);
                int payload = 0;
                readVmDword(host().m_bytecode, cursor, payload);

                if (condValue != 0)
                {
                    const int previousAnchor = controlAnchor;
                    host().m_bytecode[static_cast<std::size_t>(previousAnchor)] = script::opcodeValue(script::VmOpcode::Jump);
                    const int patchedRelative = payload - previousAnchor + cursor - 1;
                    std::memcpy(host().m_bytecode.data() + static_cast<std::size_t>(previousAnchor + 1),
                                &patchedRelative, sizeof(patchedRelative));
                    cursor += 4;
                }
                else
                {
                    cursor += payload;
                }

                controlAnchor = cursor;
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!LOGIC: '%s' stack error %i", "iff", cursor);
                m_physical.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::CallScriptFunction:
            {
                int targetBytecodeOffset = 0;
                readVmDword(host().m_bytecode, cursor, targetBytecodeOffset);

                script::StackObject savedFrameObject;
                savedFrameObject.assignInt(frameBase);
                appendExecutionStackObject(savedFrameObject);

                script::StackObject returnCursorObject;
                returnCursorObject.assignInt(cursor + 4);
                appendExecutionStackObject(returnCursorObject);

                frameBase = m_physical.stackCount;
                cursor = targetBytecodeOffset;
                controlAnchor = cursor;
                break;
            }
            case script::VmOpcode::Return:
            {
                if (m_physical.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!LOGIC: '%s' stack error %i", "return", cursor - 1);

                script::StackObject returnedValue;
                bool hasReturnedValue = false;
                if (m_physical.stackCount > frameBase)
                {
                    script::StackObject* top = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                    returnedValue.copyFrom(*top);
                    hasReturnedValue = true;
                    --m_physical.stackCount;
                }

                m_physical.stackCount = frameBase;

                --m_physical.stackCount;
                script::StackObject* returnCursorObject = mutableExecutionStackStorageAt(m_physical.stackCount);
                const int returnCursor = stackObjectNumeric41F2D0(*returnCursorObject);

                --m_physical.stackCount;
                script::StackObject* savedFrameObject = mutableExecutionStackStorageAt(m_physical.stackCount);
                frameBase = stackObjectNumeric41F2D0(*savedFrameObject);
                cursor = returnCursor;
                controlAnchor = cursor;

                if (hasReturnedValue)
                    appendExecutionStackObject(returnedValue);
                break;
            }
            case script::VmOpcode::ArrayIndex:
            {
                script::StackObject* indexObject = popObject();
                arrayVmOffset = stackObjectNumeric41F2D0(*indexObject);
                arrayVmActive = 1;
                break;
            }
            default:
            {
                
                constexpr std::uint8_t kRetailResultProbeOpcode = 84;
                if (opcode == kRetailResultProbeOpcode)
                {
                    const script::StackObject* top = mutableExecutionStackStorageAt(m_physical.stackCount - 1);
                    if (stackObjectNumeric41F2D0(*top) == arg3)
                        result = 1;
                }
                dispatchNativeFunction(static_cast<int>(opcode));
                break;
            }
            }
        }

        restoreStack();
        return result;
    }


    const char* SCRIPT::stringTextPointer(const STRING& value) const
    {
        return value.c_str();
    }

    int SCRIPT::writeCStringToStream(const STRING& source, BaseStream* target) const
    {
        const char* const text = source.c_str();
        return target->write(text, static_cast<unsigned>(std::strlen(text) + 1));
    }

    std::size_t SCRIPT::writeCStringRecord(const STRING& value, std::FILE* file) const
    {
        const char* text = value.c_str();
        const std::size_t sizeWithNul = std::strlen(text) + 1;
        return std::fwrite(text, sizeWithNul, 1, file);
    }

    std::FILE* SCRIPT::openScriptFile(const STRING& path, const char* mode) const
    {
        
        const char* text = path.c_str();
        if (text[0] == '\0')
            return nullptr;
        return std::fopen(text, mode);
    }


    int SCRIPT::popSpriteReferenceValue()
    {
        const int oldIndex = m_physical.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);

        if (top->intValue != 0 && (top->flags & script::STACK_OBJECT_REF) == 0)
            LOG::ResourceError("LOGIC", 10, "this variable is not unit", 0);

        --m_physical.stackCount;
        return stackValueToInteger(*top);
    }

    void SCRIPT::pushIntegerValue(int value)
    {
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        appendExecutionStackObject(obj);
    }

    void SCRIPT::pushSpriteReferenceValue(int value)
    {
        script::StackObject obj;
        obj.initializeReferenceValue(value);
        appendExecutionStackObject(obj);
    }

    void SCRIPT::pushStringValue(const STRING& value)
    {
        STRING ebxTemp;
        ebxTemp.AssignAllocatedCopyWithoutRelease(value.c_str());

#if defined(_MSC_VER) && defined(_M_IX86)
        std::uint32_t retailScratch;
#else
        std::uint32_t retailScratch = 0;
#endif
        const int rawScratch = static_cast<int>(core::retailReadStackDword(&retailScratch));

        appendExecutionStackRecord(
            static_cast<std::uint8_t>(script::STACK_OBJECT_STRING),
            rawScratch,
            ebxTemp);

        ebxTemp.ReleaseOwnedStorage();
    }

    int SCRIPT::popIntegerValue()
    {
        const int oldIndex = m_physical.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
        --m_physical.stackCount;
        return stackValueToInteger(*top);
    }



    GammaRawPair SCRIPT::decodePackedGamma(int value)
    {
        const std::uint32_t ecx = static_cast<std::uint32_t>(value);
        std::uint32_t diffuse = 0;
        std::uint32_t specular = 0;

        std::uint32_t edx = ecx;
        if ((ecx & 0x00000080u) != 0)
            specular |= (((~edx) & 0x0000007Fu) << 1);
        else
            diffuse |= ((edx & 0x0000007Fu) << 1);

        edx = ecx;
        if ((ecx & 0x00008000u) != 0)
            specular |= (((~edx) & 0x00007F80u) << 1);
        else
            diffuse |= ((edx & 0x00007F80u) << 1);

        edx = ecx;
        if ((ecx & 0x00800000u) != 0)
            specular |= (((~edx) & 0x007F8000u) << 1);
        else
            diffuse |= ((edx & 0x007F8000u) << 1);

        if ((ecx & 0x80000000u) != 0)
            specular |= (((~ecx) & 0xFF800000u) << 1);
        else
            diffuse |= ((ecx & 0xFF800000u) << 1);

        return GammaRawPair{diffuse, specular};
    }

    VID* SCRIPT::popVidValue(const char* errorContext)
    {
        
        const int nvid = popIntegerValue();
        VID* vid = MAP::NullVid();
        core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        if (nvid >= 0 && nvid < vidTable.count())
        {
            if (VID* const slot = vidTable.slot(nvid))
                vid = slot;
        }
        if (vid == MAP::NullVid())
        {
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", errorContext, nvid);
            return MAP::NullVid();
        }
        return vid;
    }

    void SCRIPT::reportScriptError(int errorCode, const char* text, int value)
    {
        
        LOG::ResourceError("SCRIPT", errorCode, text, value);
    }


    int SCRIPT::topValueIsString() const
    {
        const script::StackObject& top = host().m_executionStack[static_cast<std::size_t>(m_physical.stackCount - 1)];
        return (top.flags & script::STACK_OBJECT_STRING) != 0 ? 1 : 0;
    }

    STRING SCRIPT::popStringValue()
    {
        const int oldIndex = m_physical.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
        --m_physical.stackCount;

        if ((top->flags & script::STACK_OBJECT_INT) != 0)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "%d", top->intValue);
            top->text.Assign(buffer);
        }
        return top->text;
    }

    const STRING& SCRIPT::normalizeStackValueToString(script::StackObject& value)
    {
        if ((value.flags & script::STACK_OBJECT_INT) != 0)
        {
            char numericTextBuffer[0x80];
            std::memset(numericTextBuffer, 0, sizeof(numericTextBuffer));
#if defined(_MSC_VER)
            _itoa(value.intValue, numericTextBuffer, 10);
#else
            std::snprintf(numericTextBuffer, sizeof(numericTextBuffer), "%d", value.intValue);
#endif

            STRING convertedText;
            convertedText.AssignAllocatedCopyWithoutRelease(numericTextBuffer);
            assignStringFromString(value.text, convertedText);
            convertedText.ReleaseOwnedStorage();
        }
        return value.text;
    }


    void SCRIPT::pushStringResult(const STRING& value)
    {
        
        pushStringValue(value);
    }

    void SCRIPT::pushIntegerResult(int value)
    {
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        appendExecutionStackObject(obj);
    }


    int SCRIPT::dispatchNativeFunction(int opcode)
    {
        SCRIPT* const scriptOwner = core::ApplicationScriptRuntime();
        return scriptOwner->executeNativeFunction(opcode, core::GlobalApplicationDrawDispatcherState());
    }

    int SCRIPT::executeNativeFunction(int opcode, core::ApplicationDrawDispatcherState& drawState)
    {
        switch (static_cast<script::NativeFunctionCode>(opcode))
        {
        case script::NativeFunctionCode::Save:
            return execFuncCase99Save();
        case script::NativeFunctionCode::MenuCreate:
            return execFuncCase107MenuCreate();
        case script::NativeFunctionCode::MessageText:
            return execFuncCase114MessageText();
        case script::NativeFunctionCode::SetShiftCoor:
            return execFuncCase116SetShiftCoor();
        case script::NativeFunctionCode::SetApplicationFlag7:
            return execFuncCase121SetApplicationFlag7();
        case script::NativeFunctionCode::PlayerNoop:
            return execFuncCase122PlayerNoop();
        case script::NativeFunctionCode::ExecuteShellFile:
            return execFuncCase152ShellExecute();
        case script::NativeFunctionCode::ChangeZUnit:
            return execFuncCase156ChangeZUnit(drawState);
        case script::NativeFunctionCode::ReplaceUnit:
            return execFuncCase170ReplaceUnit(drawState);
        case script::NativeFunctionCode::TrainProperty:
            return execFuncCase212TrainProperty();
        case script::NativeFunctionCode::SetSemaphore:
            return execFuncCase231SetSemaphore();
        case script::NativeFunctionCode::BreakTrain:
            return execFuncCase239BreakTrain();
        case script::NativeFunctionCode::FirstTrain:
            return execFuncCase240FirstTrain();
        case script::NativeFunctionCode::NextTrain:
            return execFuncCase241NextTrain();
        case script::NativeFunctionCode::PatrolEngine:
            return execFuncCase242PatrolEngine();
        case script::NativeFunctionCode::SetPushLine:
            return execFuncCase243SetPushLine();
        case script::NativeFunctionCode::PlayerPathFlag:
            return execFuncCase246PlayerPathFlag();
        case script::NativeFunctionCode::AddUnitLimit:
            return execFuncCase249AddUnitLimit();
        case script::NativeFunctionCode::SetMoney:
            return execFuncCase251SetMoney();
        case script::NativeFunctionCode::GetMoney:
            return execFuncCase252GetMoney();
        case script::NativeFunctionCode::Noop253:
            return execFuncCase253Noop();
        case script::NativeFunctionCode::Noop254:
            return execFuncCase254Noop();
        case script::NativeFunctionCode::CreateSprite:
            return execFuncCase65CreateSprite();
        case script::NativeFunctionCode::Load:
            return execFuncCase98Load();
        case script::NativeFunctionCode::MenuFind:
            return execFuncCase101MenuFind();
        case script::NativeFunctionCode::MenuLoad:
            return execFuncCase102MenuLoad();
        case script::NativeFunctionCode::MenuRelease:
            return execFuncCase103MenuRelease();
        case script::NativeFunctionCode::MenuNvidUnderCursor:
            return execFuncCase104MenuNvidUnderCursor();
        case script::NativeFunctionCode::MenuNdirUnderCursor:
            return execFuncCase105MenuNdirUnderCursor();
        case script::NativeFunctionCode::MenuAction:
            return execFuncCase106MenuAction();
        case script::NativeFunctionCode::Flagman:
            return execFuncCase66Flagman();
        case script::NativeFunctionCode::FirstUnit:
            return execFuncCase68FirstUnit();
        case script::NativeFunctionCode::NextUnit:
            return execFuncCase69NextUnit();
        case script::NativeFunctionCode::GetSprite:
            return execFuncCase70GetSprite();
        case script::NativeFunctionCode::GetSpriteScr:
            return execFuncCase71GetSpriteScr();
        case script::NativeFunctionCode::FindNearestSprite:
            return execFuncCase72FindNearestSprite();
        case script::NativeFunctionCode::FirstInBox:
            return execFuncCase74FirstInBox();
        case script::NativeFunctionCode::NextInBox:
            return execFuncCase75NextInBox();
        case script::NativeFunctionCode::FirstSprite:
            return execFuncCase76FirstSprite(drawState);
        case script::NativeFunctionCode::NextSprite:
            return execFuncCase77NextSprite(drawState);
        case script::NativeFunctionCode::Action:
            return execFuncCase79Action();
        case script::NativeFunctionCode::AddCommand:
            return execFuncCase82AddCommand();
        case script::NativeFunctionCode::GetCommands:
            return execFuncCase96GetCommands();
        case script::NativeFunctionCode::SetCommands:
            return execFuncCase97SetCommands();
        case script::NativeFunctionCode::SaveDemo:
            return execFuncCase100SaveDemo();
        case script::NativeFunctionCode::GetUnitVid:
            return execFuncCase83GetUnitVid();
        case script::NativeFunctionCode::Destroy:
            return execFuncCase84Destroy();
        case script::NativeFunctionCode::GetX:
            return execFuncCase85GetX();
        case script::NativeFunctionCode::GetY:
            return execFuncCase86GetY();
        case script::NativeFunctionCode::GetZ:
            return execFuncCase87GetZ();
        case script::NativeFunctionCode::GetDirection:
            return execFuncCase88GetDirection();
        case script::NativeFunctionCode::GetAnimation:
            return execFuncCase89GetAnimation();
        case script::NativeFunctionCode::MenuLeftClick:
            return execFuncCase108MenuLclick();
        case script::NativeFunctionCode::GetInputX:
            return execFuncCase109GetInputX();
        case script::NativeFunctionCode::GetInputY:
            return execFuncCase110GetInputY();
        case script::NativeFunctionCode::GetKey:
            return execFuncCase111GetKey();
        case script::NativeFunctionCode::GetInputState:
            return execFuncCase115GetInputState();
        case script::NativeFunctionCode::SizeTo:
            return execFuncCase80SizeTo();
        case script::NativeFunctionCode::DirectionTo:
            return execFuncCase90DirectionTo();
        case script::NativeFunctionCode::ScreenX:
            return execFuncCase119ScreenX();
        case script::NativeFunctionCode::ScreenY:
            return execFuncCase120ScreenY();
        case script::NativeFunctionCode::GetString:
            return execFuncCase123GetString();
        case script::NativeFunctionCode::Exit:
            return execFuncCase124Exit();
        case script::NativeFunctionCode::ToScreenX:
            return execFuncCase125ToScreenX();
        case script::NativeFunctionCode::ToScreenY:
            return execFuncCase126ToScreenY();
        case script::NativeFunctionCode::SetScrollType:
            return execFuncCase117SetScrollType();
        case script::NativeFunctionCode::GetScrollType:
            return execFuncCase118GetScrollType();
        case script::NativeFunctionCode::MenuRightClick:
            return execFuncCase127MenuRclick();
        case script::NativeFunctionCode::SetMouseClick:
            return execFuncCase128SetMouseClick();
        case script::NativeFunctionCode::SetSoundVolume:
            return execFuncCase130SetSoundVolume();
        case script::NativeFunctionCode::SetMusicVolume:
            return execFuncCase131SetMusicVolume();
        case script::NativeFunctionCode::PlaySfx:
            return execFuncCase132PlaySfx();
        case script::NativeFunctionCode::StopSfx:
            return execFuncCase133StopSfx();
        case script::NativeFunctionCode::StopMusic:
            return execFuncCase134StopMusic();
        case script::NativeFunctionCode::PlaySfxFromCoor:
            return execFuncCase135PlaySfxFromCoor();
        case script::NativeFunctionCode::PlayMusicFile:
            return execFuncCase136PlayMusicFile();
        case script::NativeFunctionCode::Effect:
            return execFuncCase137Effect();
        case script::NativeFunctionCode::SetEnvironment:
            return execFuncCase138SetEnvironment();
        case script::NativeFunctionCode::SetGraphDetail:
            return execFuncCase139SetGraphDetail();
        case script::NativeFunctionCode::SetGamma:
            return execFuncCase140SetGamma();
        case script::NativeFunctionCode::SetWind:
            return execFuncCase141SetWind();
        case script::NativeFunctionCode::CountGamma:
            return execFuncCase146CountGamma();
        case script::NativeFunctionCode::GetGamma:
            return execFuncCase147GetGamma();
        case script::NativeFunctionCode::GetEffectState:
            return execFuncCase148GetEffectState();
        case script::NativeFunctionCode::PlayMovie:
            return execFuncCase142PlayMovie();
        case script::NativeFunctionCode::IsPlayMovie:
            return execFuncCase143IsPlayMovie();
        case script::NativeFunctionCode::StopMovie:
            return execFuncCase144StopMovie();
        case script::NativeFunctionCode::IsPlayMusic:
            return execFuncCase145IsPlayMusic();
        case script::NativeFunctionCode::SetAutoReBirth:
            return execFuncCase247SetAutoReBirth();
        case script::NativeFunctionCode::SetEnemyCanAttackNeutralTrains:
            return execFuncCase250SetEnemyCanAttackNeutralTrains();
        case script::NativeFunctionCode::GetScreenInputX:
            return execFuncCase244GetScreenInputX();
        case script::NativeFunctionCode::GetScreenInputY:
            return execFuncCase245GetScreenInputY();
        case script::NativeFunctionCode::CharAt:
            return execFuncCase153Charat();
        case script::NativeFunctionCode::Log:
            return execFuncCase154Log();
        case script::NativeFunctionCode::Random:
            return execFuncCase155Random();
        case script::NativeFunctionCode::GetTime:
            return execFuncCase157GetTime();
        case script::NativeFunctionCode::GetGroundZ:
            return execFuncCase158GetGroundZ();
        case script::NativeFunctionCode::StringLength:
            return execFuncCase159Strlen();
        case script::NativeFunctionCode::SetFlagman:
            return execFuncCase160SetFlagman();
        case script::NativeFunctionCode::AskPlace:
            return execFuncCase161AskPlace();
        case script::NativeFunctionCode::GetVidData:
            return execFuncCase162GetVidData();
        case script::NativeFunctionCode::SetVidData:
            return execFuncCase163SetVidData();
        case script::NativeFunctionCode::IntToString:
            return execFuncCase164Itoa();
        case script::NativeFunctionCode::Sin:
            return execFuncCase165Sin();
        case script::NativeFunctionCode::Cos:
            return execFuncCase166Cos();
        case script::NativeFunctionCode::MapSizeX:
            return execFuncCase167MapSizeX();
        case script::NativeFunctionCode::MapSizeY:
            return execFuncCase168MapSizeY();
        case script::NativeFunctionCode::Genocide:
            return execFuncCase169Genocide(drawState);
        case script::NativeFunctionCode::Printf:
            return execFuncCase172Printf();
        case script::NativeFunctionCode::ReloadVid:
            return execFuncCase173ReloadVid();
        case script::NativeFunctionCode::FileWrite:
            return execFuncCase174Fwrite();
        case script::NativeFunctionCode::FileRead:
            return execFuncCase175Fread();
        case script::NativeFunctionCode::FileOpen:
            return execFuncCase176Fopen();
        case script::NativeFunctionCode::FileClose:
            return execFuncCase177Fclose();
        case script::NativeFunctionCode::FileCreate:
            return execFuncCase178Fcreate();
        case script::NativeFunctionCode::FileEof:
            return execFuncCase179Feof();
        case script::NativeFunctionCode::RegistryGet:
            return execFuncCase182GetReg();
        case script::NativeFunctionCode::RegistrySet:
            return execFuncCase183SetReg();
        case script::NativeFunctionCode::RegistryDelete:
            return execFuncCase184DelReg();
        case script::NativeFunctionCode::RegistryDefaultPath:
            return execFuncCase185GetDefaultRegPath();
        case script::NativeFunctionCode::LegacyHandler:
            return execFuncCase70Handler(drawState);
        case script::NativeFunctionCode::SetCursor:
            return execFuncCase113SetCursor();
        default:
            writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: Unknown extern Function %i", opcode);
            return 0;
        }
    }


    SPRITE* SCRIPT::popSpriteReference()
    {
        return scriptResolveSpriteReference(popSpriteReferenceValue());
    }

    void SCRIPT::pushSpriteReference(SPRITE* sprite)
    {
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
    }

    int SCRIPT::execFuncCase99Save()
    {
        const STRING path = popStringValue();
#ifdef _WIN32
        win::applicationWinInstance()->saveMap(path);
#else
        if (MAP* const map = MAP::Current())
            map->saveMapHost(path);
#endif
        return 0;
    }

    int SCRIPT::execFuncCase107MenuCreate()
    {
        const int z = popIntegerValue();
        const int yDelta = popIntegerValue();
        const int y = z + yDelta;
        const int x = popIntegerValue();
        const int directionSource = popIntegerValue();
        const int nvid = popIntegerValue();

        VID* const vid = resolveVidByNvid(nvid);
        if (vid == MAP::NullVid())
        {
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", "for MenuCreate", nvid);
            pushIntegerValue(0);
            return 0;
        }

        const int direction = ((directionSource << 8) / static_cast<int>(vid->directionCount())) & 0xFF;
        SPRITE* created = nullptr;
        if (MAP* const map = MAP::Current())
            created = map->CreateSpriteViaFactory(
                vid, VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                ANGLE(direction), nullptr, false);
        pushSpriteReference(created);
        return 0;
    }

    int SCRIPT::execFuncCase114MessageText()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        STRING text = popStringValue();
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
        {
            PLAYER* const player = app->startupPlayerSlotByIndex(
                static_cast<int>(app->activeStartupPlayerIndex()));
            if (player)
                player->submitPathCoordinate(&text, static_cast<float>(x), static_cast<float>(y));
        }
#endif
        return 0;
    }

    int SCRIPT::execFuncCase116SetShiftCoor()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        if (MAP* const map = MAP::Current())
            map->SetShiftCoor(static_cast<float>(x), static_cast<float>(y), 0);
        return 0;
    }

    int SCRIPT::execFuncCase121SetApplicationFlag7()
    {
        const int value = popIntegerValue();
        std::uint32_t flags = core::ApplicationFlags();
        flags = (flags & ~application_flags::ScriptControlBit7) |
            (value != 0 ? application_flags::ScriptControlBit7 : 0u);
        core::SetApplicationFlags(flags);
        return 0;
    }

    int SCRIPT::execFuncCase122PlayerNoop()
    {
        (void)popIntegerValue();
        // Retail active PLAYER vtable +0x1C/+0x20 both resolve to nullsub_3.
        return 0;
    }

    int SCRIPT::execFuncCase152ShellExecute()
    {
        lpFile.Assign(popStringValue().c_str());
#ifdef _WIN32
        ShellExecuteA(nullptr, nullptr, lpFile.c_str(), nullptr, nullptr, 5);
#endif
        return 0;
    }

    int SCRIPT::execFuncCase156ChangeZUnit(core::ApplicationDrawDispatcherState& drawState)
    {
        const int z = popIntegerValue();
        VID* const vid = popVidValue("for ChangeZUnit");
        if (vid == MAP::NullVid())
            return 0;

        const int pass = vid->renderLayer();
        int cursor = drawState.drawPassBucket(pass).count();
        SPRITE* sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
        while (sprite)
        {
            if (sprite->Vid() == vid)
                sprite->ChangeCoor(sprite->X(), sprite->Y(), static_cast<float>(z));
            sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
        }
        return 0;
    }

    int SCRIPT::execFuncCase170ReplaceUnit(core::ApplicationDrawDispatcherState& drawState)
    {
        VID* const replacement = popVidValue("for Replace Unit 2");
        VID* const source = popVidValue("for Replace Unit 1");
        if (replacement == MAP::NullVid() || source == MAP::NullVid())
            return 0;

        const int pass = source->renderLayer();
        int cursor = 0;
        SPRITE* sprite = beginReverseDrawPassIteration(drawState, pass, &cursor);
        while (sprite)
        {
            if (sprite->Vid() == source)
            {
                if (MAP* const map = MAP::Current())
                    map->CreateSpriteViaFactory(
                        replacement, VECTOR(sprite->X(), sprite->Y(), sprite->Z()),
                        ANGLE(sprite->directionIndex()), nullptr, false);
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
            }
            sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
        }
        return 0;
    }

    int SCRIPT::execFuncCase212TrainProperty()
    {
        const int property = popIntegerValue();
        SPRITE* const sprite = popSpriteReference();
        if (!sprite || sprite->Vid()->spriteClassId() != 21)
        {
            pushIntegerResult(0);
            return 0;
        }

        SPRITE::EngineChainMetrics range{};
        range.collectEngineChainMetrics(sprite);
        switch (property)
        {
        case 1: pushIntegerResult(range.movementDelayMs); return 0;
        case 2: pushIntegerResult(range.routeMetricSum); return 0;
        case 3: pushIntegerResult(100 * range.spriteFrameTimeSum / range.vidFrameTimeSum); return 0;
        case 4: pushIntegerResult(range.spriteFrameTimeSum); return 0;
        case 5: pushIntegerResult(range.averageDistanceRatio); return 0;
        case 6:
            pushIntegerResult(range.weaponRatioScaledByEight());
            return 0;
        case 7: pushIntegerResult(range.weaponMetricSum); return 0;
        case 9:
            for (SPRITE* node = sprite->engineChainHead(); node; node = node->engineChainNext())
            {
                if (((node->runtimeFlags() >> 2) & 0x1Fu) != 0u)
                    pushIntegerResult(0);
            }
            pushIntegerResult(1);
            return 0;
        case 10: pushIntegerResult(range.fixedDistanceSum); return 0;
        case 11: pushIntegerResult(range.distanceWeightSum); return 0;
        default: pushIntegerResult(0); return 0;
        }
    }

    int SCRIPT::execFuncCase231SetSemaphore()
    {
        const int unused = popIntegerValue();
        const int value = popIntegerValue();
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        core::setNearestLinkValue(&core::globalWeakControllerMap(), x, y, value, unused);
        return 0;
    }

    int SCRIPT::execFuncCase239BreakTrain()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        if (SPRITE* const sprite = popSpriteReference())
            sprite->splitEngineChainAtPosition(static_cast<float>(x), static_cast<float>(y));
        return 0;
    }

    int SCRIPT::execFuncCase240FirstTrain()
    {
        pushSpriteReference(beginRootUnitArmyIteration(popIntegerValue()));
        return 0;
    }

    int SCRIPT::execFuncCase241NextTrain()
    {
        pushSpriteReference(continueRootUnitArmyIteration());
        return 0;
    }

    int SCRIPT::execFuncCase242PatrolEngine()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        SPRITE* const sprite = popSpriteReference();
        if (sprite && sprite->Vid()->spriteClassId() == 21)
        {
            sprite->dispatchEnginePrivateCommandAtPathPoint(25, x, y);
            return 0;
        }
        LOG::Write(u8"Борис, у тебя в PatrolTrain - train неверный %X",
                   static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sprite)));
        return 0;
    }

    int SCRIPT::execFuncCase243SetPushLine()
    {
        const int value = popIntegerValue();
        const int y2 = popIntegerValue();
        const int x2 = popIntegerValue();
        const int y1 = popIntegerValue();
        const int x1 = popIntegerValue();
        core::setPushLine(&core::globalWeakControllerMap(), x1, y1, x2, y2, value);
        return 0;
    }

    int SCRIPT::execFuncCase246PlayerPathFlag()
    {
        const int value = popIntegerValue();
        scriptPlayerSlot(1)->setPathSecondaryFlag(value);
        return 0;
    }

    int SCRIPT::execFuncCase249AddUnitLimit()
    {
        const int index = popIntegerValue();
        VID* const vid = popVidValue("for AddUnitLimit");
        const int value = popIntegerValue();
        if (vid != MAP::NullVid())
            vid->setUnitLimit(index, value);
        return 0;
    }

    int SCRIPT::execFuncCase251SetMoney()
    {
        const int value = popIntegerValue();
        const int playerIndex = popIntegerValue();
        scriptPlayerSlot(playerIndex)->setMoney(static_cast<DWORD>(value));
        return 0;
    }

    int SCRIPT::execFuncCase252GetMoney()
    {
        const int playerIndex = popIntegerValue();
        pushIntegerResult(static_cast<int>(scriptPlayerSlot(playerIndex)->getMoney()));
        return 0;
    }

    int SCRIPT::execFuncCase253Noop()
    {
        (void)popIntegerValue();
        (void)popIntegerValue();
        (void)popSpriteReference();
        return 0;
    }

    int SCRIPT::execFuncCase254Noop()
    {
        (void)popSpriteReference();
        (void)popSpriteReference();
        return 0;
    }




    int SCRIPT::execFuncCase108MenuLclick()
    {
        const int value = static_cast<int>(applicationFrameSpriteList().selectionFlags() & 1u);
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase109GetInputX()
    {
        pushIntegerValue(static_cast<int>(scriptApplicationInputState230().worldX));
        return 0;
    }

    int SCRIPT::execFuncCase110GetInputY()
    {
        pushIntegerValue(static_cast<int>(scriptApplicationInputState230().worldY));
        return 0;
    }

    int SCRIPT::execFuncCase111GetKey()
    {
        const int value = static_cast<int>(scriptApplicationInputState230().lastCode);
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase115GetInputState()
    {
        const std::uint32_t state = scriptApplicationInputState230().flags;
        const int bitOrder[] = {15, 14, 9, 10, 8, 7, 12, 11, 6, 5, 2, 0};
        int packed = 0;
        for (int bit : bitOrder)
            packed = (packed << 1) | static_cast<int>((state >> bit) & 1u);
        pushIntegerValue(packed);
        return 0;
    }

    int SCRIPT::execFuncCase113SetCursor()
    {
        const int cursorId = popIntegerValue();
        MOUSE* mouse = mouseInstanceRef();
        if (!mouse)
            return 0;

        if (cursorId < 0)
            mouse->HardwareOff();
        else
        {
            mouse->HardwareOn();
            mouse = mouseInstanceRef();
            if (mouse)
                mouse->setCursorId(cursorId);
        }
        return 0;
    }


    int SCRIPT::execFuncCase98Load()
    {
        
        const STRING path = popStringValue();
        const std::uint32_t flags = core::ApplicationFlags() | application_flags::PendingCommandOrLoad;
        core::SetApplicationFlags(flags);
#ifdef _WIN32
        win::applicationWinInstance()->setPendingCommand(path);
        win::applicationWinInstance()->setFlags(flags);
#else
        if (host().m_nativeContext.queueMapLoadSlot18Flag40)
            host().m_nativeContext.queueMapLoadSlot18Flag40(path);
#endif
        return 0;
    }

    int SCRIPT::execFuncCase101MenuFind()
    {
        const int ndir = popIntegerValue();
        const int nvid = popIntegerValue();
        VID* const vid = resolveVidByNvid(nvid);
        if (vid == MAP::NullVid())
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", "for MenuFind", nvid);

        SPRITE* found = nullptr;
        if (vid != MAP::NullVid())
        {
            SPRITE_LIST& list = applicationFrameSpriteList();
            const int count = list.activeCount();
            for (int i = 0; i < count; ++i)
            {
                SPRITE* const sprite = list.at(i);
                if (!sprite || sprite->Vid() != vid)
                    continue;
                if (ndir != 999999)
                {
                    const std::uint32_t directionByte =
                        static_cast<std::uint32_t>(sprite->directionIndex()
                            + vid->directionQuantizationOffset()) & 0xFFu;
                    const int directionIndex = static_cast<int>(
                        (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                    if (directionIndex != ndir)
                        continue;
                }
                found = sprite;
                break;
            }
        }
        pushSpriteReferenceValue(scriptSpritePointerValue(found));
        return 0;
    }

    int SCRIPT::execFuncCase102MenuLoad()
    {
        const STRING path = popStringValue();
        (void)applicationFrameSpriteList().loadMenuSpriteList(path);
        return 0;
    }

    int SCRIPT::execFuncCase103MenuRelease()
    {
        (void)applicationFrameSpriteList().releaseRepeatedReferencesRetail();
        return 0;
    }

    int SCRIPT::execFuncCase104MenuNvidUnderCursor()
    {
        pushIntegerValue(applicationFrameSpriteList().selectedSpriteNvid());
        return 0;
    }

    int SCRIPT::execFuncCase105MenuNdirUnderCursor()
    {
        pushIntegerValue(applicationFrameSpriteList().selectedSpriteDirectionFrame());
        return 0;
    }

    int SCRIPT::execFuncCase106MenuAction()
    {
        const int var3 = popIntegerValue();
        const int var2 = popIntegerValue();
        const int var1 = popIntegerValue();
        const int action = popIntegerValue();
        const int ndir = popIntegerValue();
        const int nvid = popIntegerValue();

        VID* const vid = resolveVidByNvid(nvid);
        if (!vid || vid == MAP::NullVid())
        {
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", "for MenuAction", nvid);
            return 0;
        }

        SPRITE_LIST& list = applicationFrameSpriteList();
        const int count = list.activeCount();
        for (int i = 0; i < count; ++i)
        {
            SPRITE* const sprite = list.at(i);
            if (!sprite || sprite->Vid() != vid)
                continue;
            if (ndir != 999999)
            {
                const std::uint32_t directionByte =
                    static_cast<std::uint32_t>(sprite->directionIndex()
                        + vid->directionQuantizationOffset()) & 0xFFu;
                const int directionIndex = static_cast<int>(
                    (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                if (directionIndex != ndir)
                    continue;
            }

            if (action < 0x11)
                sprite->ChangeAnimation(action);
            else
                (void)sprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(action), var1, var2, var3);
        }
        return 0;
    }

    int SCRIPT::execFuncCase124Exit()
    {
        (void)popStringValue();
#ifdef _WIN32
        if (win::ApplicationWin* const app = win::applicationWinInstance())
            if (HWND hwnd = app->nativeWindow())
                ::PostMessageA(hwnd, WM_CLOSE, 0, 0);
#endif
        return 0;
    }

    int SCRIPT::execFuncCase161AskPlace()
    {
        const int z = popIntegerValue();
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int nvid = popIntegerValue();
        VID* const vid = resolveVidByNvid(nvid);
        if (!vid || vid == MAP::NullVid())
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", "for AskPlace", nvid);

        int handle = 0;
        if (MAP* const map = MAP::Current())
        {
            if (SPRITE* const hit = GlobalHashQueryCellCollisionByVid(
                    *map, vid, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)))
            {
                handle = scriptSpritePointerValue(hit);
            }
        }
        pushSpriteReferenceValue(handle);
        return 0;
    }

    int SCRIPT::execFuncCase169Genocide(core::ApplicationDrawDispatcherState& drawState)
    {
        VID* const vid = popVidValue("for Genocide");
        if (!vid || vid == MAP::NullVid())
            return 0;

        const int pass = vid->renderLayer();
        int cursor = 0;
        SPRITE* sprite = beginReverseDrawPassIteration(drawState, pass, &cursor);
        while (sprite)
        {
            if (sprite->Vid() == vid)
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
            sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
        }
        return 0;
    }

    int SCRIPT::execFuncCase65CreateSprite()
    {
        const int parentHandle = popSpriteReferenceValue();
        const int direction = popIntegerValue();
        const int z = popIntegerValue();
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int nvid = popIntegerValue();
        VID* const vid = resolveVidByNvid(nvid);
        if (vid == MAP::NullVid())
        {
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", "for CreateSprite()", nvid);
            pushIntegerValue(0);
            return 0;
        }

        MAP* const map = MAP::Current();
        SPRITE* const parent = scriptResolveSpriteReference(parentHandle);
        SPRITE* const created = map->CreateSpriteViaFactory(
            vid,
            VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
            ANGLE(direction),
            parent,
            false);
        pushSpriteReferenceValue(scriptSpritePointerValue(created));
        return 0;
    }

    int SCRIPT::execFuncCase66Flagman()
    {
        const int army = popIntegerValue();
        SPRITE* sprite = nullptr;
#ifdef _WIN32
        sprite = win::applicationWinInstance()->controlledSpriteForPlayer(army);
#else
        sprite = MAP::Current()->flagmanSpriteForPlayer(army);
#endif
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase68FirstUnit()
    {
        g_scriptUnitIteratorTypeMask = popIntegerValue();
        g_scriptUnitIteratorCursor = 0;
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
        SPRITE* sprite = list.beginReverseIteration(&g_scriptUnitIteratorCursor);
        while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & g_scriptUnitIteratorTypeMask) == 0))
            sprite = list.continueReverseIteration(&g_scriptUnitIteratorCursor);
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase69NextUnit()
    {
        SPRITE_COLLECTOR_HASH_MAP* const hash = GlobalSpriteHashMap();
        SPRITE_POINTER_LIST& list = hash->mutableOverflowList();
        SPRITE* sprite = list.continueReverseIteration(&g_scriptUnitIteratorCursor);
        while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & g_scriptUnitIteratorTypeMask) == 0))
            sprite = list.continueReverseIteration(&g_scriptUnitIteratorCursor);
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase70GetSprite()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int type = popIntegerValue();
        SPRITE* const sprite = core::Application::findSpriteAtPointByBounds(
            *MAP::Current(), core::GlobalApplicationDrawDispatcherState(),
            type, static_cast<float>(x), static_cast<float>(y));
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase71GetSpriteScr()
    {
        const int screenY = popIntegerValue();
        const int screenX = popIntegerValue();
        const int type = popIntegerValue();
        SPRITE* const sprite = core::Application::findSpriteAtPointByFilter(
            *MAP::Current(), core::GlobalApplicationDrawDispatcherState(),
            type, static_cast<float>(screenX), static_cast<float>(screenY));
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase72FindNearestSprite()
    {
        const int radius = popIntegerValue();
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int type = popIntegerValue();
        SPRITE* const sprite = core::Application::findNearestSpriteByFilter(
            *MAP::Current(), core::GlobalApplicationDrawDispatcherState(),
            type, static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius));
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase74FirstInBox()
    {
        const int bottom = popIntegerValue();
        const int right = popIntegerValue();
        const int top = popIntegerValue();
        const int left = popIntegerValue();
        SPRITE* sprite = GlobalHashFirstInBoxAroundDot(
            static_cast<float>(left),
            static_cast<float>(top),
            static_cast<float>(right),
            static_cast<float>(bottom));
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase75NextInBox()
    {
        SPRITE* sprite = GlobalHashNextInBoxAroundDot();
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase76FirstSprite(core::ApplicationDrawDispatcherState& drawState)
    {
        g_scriptSpriteIteratorPass = 0;
        g_scriptSpriteIteratorCursor = drawState.drawPassBucket(0).count();
        SPRITE* sprite = core::Application::previousSpriteInDrawPass(drawState, 0, &g_scriptSpriteIteratorCursor);
        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }

    int SCRIPT::execFuncCase77NextSprite(core::ApplicationDrawDispatcherState& drawState)
    {
        SPRITE* sprite = nullptr;

        --g_scriptSpriteIteratorCursor;
        if (g_scriptSpriteIteratorCursor >= 0)
        {
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(g_scriptSpriteIteratorPass);
            while (g_scriptSpriteIteratorCursor >= 0)
            {
                sprite = bucket.spriteAt(g_scriptSpriteIteratorCursor);
                if (sprite)
                    break;
                --g_scriptSpriteIteratorCursor;
            }
        }

        while (!sprite && g_scriptSpriteIteratorPass < 13)
        {
            ++g_scriptSpriteIteratorPass;
            g_scriptSpriteIteratorCursor = drawState.drawPassBucket(g_scriptSpriteIteratorPass).count();
            sprite = core::Application::previousSpriteInDrawPass(drawState, g_scriptSpriteIteratorPass, &g_scriptSpriteIteratorCursor);
        }

        pushSpriteReferenceValue(scriptSpritePointerValue(sprite));
        return 0;
    }



    int SCRIPT::execFuncCase79Action()
    {
        const int var3 = popIntegerValue();
        const int var2 = popIntegerValue();
        const int var1 = popIntegerValue();
        const int act = popIntegerValue();
        SPRITE* const actionSprite = scriptResolveSpriteReference(popSpriteReferenceValue());

        if (!actionSprite)
        {
            pushIntegerValue(0);
            return 0;
        }

        if (act < 17)
        {
            actionSprite->ChangeAnimation(act);
            pushIntegerValue(0);
            return 0;
        }

        if (act == 121)
        {
            const int rawStringOwner = actionSprite->dispatchVirtualAction(ActionCode::ACT_GET_TEXT, var1, var2, var3);
            const char* const* const owner = reinterpret_cast<const char* const*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(rawStringOwner)));
            STRING value(*owner);
            pushStringValue(value);
            return 0;
        }

        if (act == 90 || act == 156 || act == 155 || act == 154 || act == 101 || act == 103)
        {
            const int result = actionSprite->dispatchVirtualAction(
                static_cast<std::uint32_t>(act), var1, var2, var3);
            pushSpriteReferenceValue(result);
            return 0;
        }

        if ((act == 33 || act == 32 || act == 36 || act == 34 || act == 150 || act == 151) &&
            actionSprite->Vid()->spriteClassId() == 21u &&
            static_cast<unsigned char>(actionSprite->runtimeFlags() & SPRITE::CommandBitsMask) == 104u)
        {
            pushIntegerValue(0);
            return 0;
        }

        const int result = actionSprite->dispatchVirtualAction(
            static_cast<std::uint32_t>(act), var1, var2, var3);
        pushIntegerValue(result);
        return 0;
    }



    int SCRIPT::execFuncCase82AddCommand()
    {
        const int var3 = popIntegerValue();
        const int var2 = popIntegerValue();
        const int var1 = popIntegerValue();
        const int act = popIntegerValue();
        const int handle = popSpriteReferenceValue();
        if (handle == 0)
            return finishNativeWithoutResult();
        scriptResolveSpriteReference(handle)->queueCommandBeforeStopSentinel(static_cast<std::uint32_t>(act), var1, var2, var3);
        return 0;
    }

    int SCRIPT::execFuncCase96GetCommands()
    {
        assignStringFromCString(lpFile, Class);
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        if (!sprite)
            return finishGetCommandsResult();

        const DWORD spriteClass = sprite->Vid()->spriteClass;
        if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
        {
            STRING commandWordsText;
            sprite->serializeCommandWordsText(commandWordsText);
            assignStringFromString(lpFile, commandWordsText);
            commandWordsText.ReleaseOwnedStorage();
        }

        STRING commandRecordsText;
        sprite->serializeCommandRecordsText(commandRecordsText);
        appendStringOwner(lpFile, commandRecordsText);
        commandRecordsText.ReleaseOwnedStorage();

        return finishGetCommandsResult();
    }

    int SCRIPT::finishGetCommandsResult()
    {
        pushStringValue(lpFile);
        return 0;
    }

    int SCRIPT::execFuncCase97SetCommands()
    {
        --m_physical.stackCount;
        script::StackObject* commandObject = mutableExecutionStackStorageAt(m_physical.stackCount);
        const STRING& commandSlot = normalizeStackValueToString(*commandObject);
        assignStringFromString(lpFile, commandSlot);

        const int handle = popSpriteReferenceValue();
        if (handle == 0)
            return finishNativeWithoutResult();

        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        if (!sprite)
            return finishNativeWithoutResult();

        const DWORD spriteClass = sprite->Vid()->spriteClass;
        if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
            sprite->parseCommandWordsText(lpFile);

        static const char kCommandSectionDelimiter[] = { '\x02', '\0' };
        if (std::strstr(lpFile.c_str(), kCommandSectionDelimiter))
        {
            STRING commandRecordsOnlyText;
            constructRightOfFirstMarker(lpFile, commandRecordsOnlyText, kCommandSectionDelimiter);
            assignStringFromString(lpFile, commandRecordsOnlyText);
            commandRecordsOnlyText.ReleaseOwnedStorage();
        }

        sprite->parseCommandRecordsText(lpFile);

        return 0;
    }


    int SCRIPT::execFuncCase100SaveDemo()
    {
        RESOURCE& demoResource = MAP::Current()->demoResource();
        if (demoResource.isOpen())
            return 0;

        const int oldIndex = m_physical.stackCount - 1;
        m_physical.stackCount = oldIndex;
        script::StackObject* const top = mutableExecutionStackStorageAt(oldIndex);
        const STRING& path = normalizeStackValueToString(*top);
        openResourceFileForWrite(demoResource, path, RESOURCE::ResTypes::DEMO);
        return 0;
    }

    int SCRIPT::pushTrueNativeResult()
    {
        pushIntegerResult(1);
        return 0;
    }

    int SCRIPT::finishNativeWithoutResult()
    {
        return 0;
    }

    int SCRIPT::execFuncCase84Destroy()
    {
        const int handle = popSpriteReferenceValue();
        if (handle == 0)
            return finishNativeWithoutResult();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        MAP* const map = MAP::Current();
        map->ReleaseSpriteForScalarDeletingDestructor(sprite);
        delete sprite;
        return 0;
    }


    int SCRIPT::execFuncCase83GetUnitVid()
    {
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        pushIntegerValue(sprite && sprite->vidPointer() ? sprite->vidPointer()->nVid : 0);
        return 0;
    }

    int SCRIPT::execFuncCase85GetX()
    {
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        pushIntegerValue(sprite ? static_cast<int>(sprite->xCoordinateValue()) : 0);
        return 0;
    }

    int SCRIPT::execFuncCase86GetY()
    {
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        pushIntegerValue(sprite ? static_cast<int>(sprite->yCoordinateValue()) : 0);
        return 0;
    }

    int SCRIPT::execFuncCase87GetZ()
    {
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        pushIntegerValue(sprite ? static_cast<int>(sprite->Z()) : 0);
        return 0;
    }

    int SCRIPT::execFuncCase88GetDirection()
    {
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        pushIntegerValue(sprite ? sprite->Direction().Int() : 0);
        return 0;
    }

    int SCRIPT::execFuncCase89GetAnimation()
    {
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        pushIntegerValue(sprite ? sprite->currentAnimation() : 0);
        return 0;
    }


    int SCRIPT::execFuncCase80SizeTo()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        int value = 0xEA60;
        if (sprite)
        {
            const long double distance = approximatePlanarDistance(
                static_cast<float>(x) - sprite->X(),
                static_cast<float>(y) - sprite->Y());
            value = static_cast<int>(distance);
        }
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase90DirectionTo()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int handle = popSpriteReferenceValue();
        SPRITE* const sprite = scriptResolveSpriteReference(handle);
        int value = 0;
        if (sprite)
            value = sprite->DirectionTo(VECTOR2{static_cast<float>(x), static_cast<float>(y)}).Int();
        pushIntegerValue(value);
        return 0;
    }




    int SCRIPT::execFuncCase117SetScrollType()
    {
        core::SetApplicationScrollType(static_cast<std::uint32_t>(popIntegerValue()));
        return 0;
    }

    int SCRIPT::execFuncCase118GetScrollType()
    {
        pushIntegerValue(static_cast<int>(core::ApplicationScrollType()));
        return 0;
    }

    int SCRIPT::execFuncCase119ScreenX()
    {
        const int value = GRAPH::CurrentGraph()->SizeX();
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase120ScreenY()
    {
        const int value = GRAPH::CurrentGraph()->SizeY();
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase125ToScreenX()
    {
        const int x = popIntegerValue();
        const int value = static_cast<int>(static_cast<float>(x) - core::GlobalApplicationDrawDispatcherState().cameraShiftX());
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase126ToScreenY()
    {
        const int z = popIntegerValue();
        const int y = popIntegerValue();
        const int value = static_cast<int>(static_cast<float>(y) - static_cast<float>(z) - core::GlobalApplicationDrawDispatcherState().cameraShiftY());
        pushIntegerValue(value);
        return 0;
    }


    int SCRIPT::execFuncCase123GetString()
    {
        assignStringFromString(lpFile, STRING());
        {
            const int oldIndex = m_physical.stackCount - 1;
            script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
            --m_physical.stackCount;
            STRING keyword = normalizeStackValueToString(*top);
            assignStringFromString(lpFile, keyword);
            keyword.ReleaseOwnedStorage();
        }

        STRING section;
        {
            const int oldIndex = m_physical.stackCount - 1;
            script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
            --m_physical.stackCount;
            section = normalizeStackValueToString(*top);
        }

        const STRING& profilePath = core::StartupStringsIniPath();

        STRING defaultValue;
        STRING profileValue;
        core::profile_p::readProfileStringInto(profileValue, profilePath, section, lpFile, defaultValue);
        pushStringValue(profileValue);
        profileValue.ReleaseOwnedStorage();
        if (defaultValue.isEmpty())
            return finishNativeWithoutResult();
        defaultValue.ReleaseOwnedStorage();
        return 0;
    }


    int SCRIPT::execFuncCase127MenuRclick()
    {
        pushIntegerValue(static_cast<int>((applicationFrameSpriteList().selectionFlags() >> 1) & 1u));
        return 0;
    }

    int SCRIPT::execFuncCase128SetMouseClick()
    {
        const int vkButton = popIntegerValue();
        const int firstOrSecond = popIntegerValue();
        input::InputControlKeys& keys = input::inputControlKeys();
        if (firstOrSecond == 0x400)
        {
            keys.first0 = static_cast<std::uint32_t>(vkButton);
            keys.first1 = static_cast<std::uint32_t>(vkButton);
            return 0;
        }
        if (firstOrSecond == 0x800)
        {
            keys.second0 = static_cast<std::uint32_t>(vkButton);
            keys.second1 = static_cast<std::uint32_t>(vkButton);
            return 0;
        }
        return finishNativeWithoutResult();
    }


    int SCRIPT::execFuncCase130SetSoundVolume()
    {
        const int volume = popIntegerValue();
        sound::GlobalSoundEngine()->setMasterVolumePercent(volume);
        return 0;
    }

    int SCRIPT::execFuncCase131SetMusicVolume()
    {
        const int volume = popIntegerValue();
        sound::GlobalSoundEngine()->setMusicVolumePercent(volume);
        return 0;
    }



    int SCRIPT::execFuncCase132PlaySfx()
    {
        const int nsfx = popIntegerValue();
        sound::GlobalSoundEngine()->enqueueSoundRequest(nsfx, 0, 0);
        return 0;
    }

    int SCRIPT::execFuncCase133StopSfx()
    {
        const int nsfx = popIntegerValue();
        sound::GlobalSoundEngine()->stopSoundNumber(nsfx);
        return 0;
    }

    int SCRIPT::execFuncCase134StopMusic()
    {
        sound::GlobalSoundEngine()->closeMusicPath();
        return 0;
    }

    int SCRIPT::execFuncCase135PlaySfxFromCoor()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int nsfx = popIntegerValue();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        const float halfScreenX = static_cast<float>(graph->SizeX()) * 0.5f;
        const float halfScreenY = static_cast<float>(graph->SizeY()) * 0.5f;
        const float soundX = static_cast<float>(x) - drawState.cameraShiftX() - halfScreenX;
        const float soundY = static_cast<float>(y) - drawState.cameraShiftY() - halfScreenY;
        sound::GlobalSoundEngine()->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);
        return 0;
    }



    int SCRIPT::execFuncCase136PlayMusicFile()
    {
        const int loop = popIntegerValue();

        STRING filename;
        {
            const int oldIndex = m_physical.stackCount - 1;
            script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
            --m_physical.stackCount;
            filename = normalizeStackValueToString(*top);
        }

        sound::GlobalSoundEngine()->playMusicFile(filename.c_str(), loop);
        return 0;
    }

    int SCRIPT::execFuncCase137Effect()
    {
        const int duration = popIntegerValue();
        const int var2 = popIntegerValue();
        const int var1 = popIntegerValue();
        const int effect = popIntegerValue();
        (void)GRAPH::CurrentGraph()->setEffect(effect, var1, var2, duration);
        return 0;
    }


    int SCRIPT::execFuncCase138SetEnvironment()
    {
        const int value = popIntegerValue();
        (void)GRAPH::CurrentGraph()->updateRenderFlags(static_cast<std::uint32_t>(value));
        return 0;
    }

    int SCRIPT::execFuncCase139SetGraphDetail()
    {
        const int index = m_physical.stackCount - 1;
        m_physical.stackCount = index;
        script::StackObject* entry = mutableExecutionStackStorageAt(index);
        if ((entry->flags & script::STACK_OBJECT_STRING) == 0)
            return finishNativeWithoutResult();

        (void)script::ParseStackIntegerText(entry->text.c_str());
        return 0;
    }

    int SCRIPT::execFuncCase140SetGamma()
    {
        const int gammaIndex = popIntegerValue();
        std::uint32_t diffuse = 0;
        std::uint32_t specular = 0;
        scriptNativeDecodeGammaIndex(gammaIndex, diffuse, specular);
        GRAPH::CurrentGraph()->setGamma(diffuse, specular);
        return 0;
    }

    int SCRIPT::execFuncCase141SetWind()
    {
        const int direct = popIntegerValue();
        const int wind = popIntegerValue();
        GRAPH::CurrentGraph()->SetWind(static_cast<std::uint32_t>(direct) & 0xFFu,
                                       static_cast<float>(wind) * 0.001f);
        return 0;
    }

    int SCRIPT::execFuncCase146CountGamma()
    {
        const int time = popIntegerValue();
        const int g2 = popIntegerValue();
        const int g1 = popIntegerValue();
        pushIntegerValue(interpolateGammaColor(g1, g2, time));
        return 0;
    }

    int SCRIPT::execFuncCase147GetGamma()
    {
        const GammaRawPair& gamma = GRAPH::CurrentGraph()->rawGammaPair();
        std::uint32_t out = (gamma.first >> 1) & 0x7F7F7F7Fu;
        for (int shift = 0; shift < 32; shift += 8)
        {
            const std::uint32_t specByte = (gamma.second >> shift) & 0xFFu;
            if (specByte != 0)
            {
                const std::uint32_t packedByte = 0x80u | (((~specByte) & 0xFEu) >> 1);
                out = (out & ~(0xFFu << shift)) | ((packedByte & 0xFFu) << shift);
            }
        }
        pushIntegerValue(static_cast<int>(out));
        return 0;
    }

    int SCRIPT::execFuncCase148GetEffectState()
    {
        const int effect = popIntegerValue();
        pushIntegerResult(GRAPH::CurrentGraph()->getEffectState(effect));
        return 0;
    }


    int SCRIPT::execFuncCase142PlayMovie()
    {
        STRING filename;
        {
            const int oldIndex = m_physical.stackCount - 1;
            script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
            --m_physical.stackCount;
            filename = normalizeStackValueToString(*top);
        }

        GRAPH::CurrentGraph()->playMovieCentered(filename);
        return 0;
    }

    int SCRIPT::execFuncCase143IsPlayMovie()
    {
        const int value = GRAPH::CurrentGraph()->movieComObject(0) != nullptr ? 1 : 0;
        pushIntegerValue(value);
        return 0;
    }

    int SCRIPT::execFuncCase144StopMovie()
    {
        GRAPH::CurrentGraph()->releaseMoviePlayback();
        return 0;
    }


    int SCRIPT::execFuncCase145IsPlayMusic()
    {
        const int value = sound::GlobalSoundEngine()->isMusicPlaying() ? 1 : 0;
        pushIntegerValue(value);
        return 0;
    }


    int SCRIPT::execFuncCase244GetScreenInputX()
    {
        pushIntegerResult(static_cast<int>(scriptApplicationInputState230().clientX));
        return 0;
    }

    int SCRIPT::execFuncCase245GetScreenInputY()
    {
        pushIntegerResult(static_cast<int>(scriptApplicationInputState230().clientY));
        return 0;
    }

    int SCRIPT::execFuncCase247SetAutoReBirth()
    {
        (void)popIntegerValue();
        return 0;
    }

    int SCRIPT::execFuncCase250SetEnemyCanAttackNeutralTrains()
    {
        const int value = popIntegerValue();
        const std::uint32_t bit = (value != 0) ? application_flags::EnemyCanAttackNeutralTrains : 0u;
        const std::uint32_t flags = (core::ApplicationFlags() & ~application_flags::EnemyCanAttackNeutralTrains) | bit;
        core::SetApplicationFlags(flags);
        return 0;
    }


    int SCRIPT::execFuncCase153Charat()
    {
        const int index = popIntegerValue();
        const int oldIndex = m_physical.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
        --m_physical.stackCount;
        const STRING text = normalizeStackValueToString(*top);

        const int value = static_cast<int>(static_cast<signed char>(text.c_str()[index]));
        pushIntegerValue(value);
        return 0;
    }


    int SCRIPT::execFuncCase154Log()
    {
        
        STRING text;
        {
            const int oldIndex = m_physical.stackCount - 1;
            script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
            --m_physical.stackCount;
            text = normalizeStackValueToString(*top);
        }
        LOG::Write(text.c_str());
        return 0;
    }


    int SCRIPT::execFuncCase155Random()
    {
        const int maxValue = popIntegerValue();
        const int divisor = maxValue + 1;
        const int value = std::rand() % divisor;
        pushIntegerResult(value);
        return 0;
    }

    int SCRIPT::execFuncCase157GetTime()
    {
        pushIntegerResult(static_cast<int>(core::CurrentTimeMilliseconds()));
        return 0;
    }



    int SCRIPT::execFuncCase158GetGroundZ()
    {
        const int y = popIntegerValue();
        const int x = popIntegerValue();
        const int value = static_cast<int>(MAP::Current()->GetGroundZ(VECTOR2{static_cast<float>(x), static_cast<float>(y)}));
        pushIntegerResult(value);
        return 0;
    }

    int SCRIPT::execFuncCase159Strlen()
    {
        STRING text;
        {
            const int oldIndex = m_physical.stackCount - 1;
            script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
            --m_physical.stackCount;
            text = normalizeStackValueToString(*top);
        }
        pushIntegerValue(text.Length());
        return 0;
    }


    int SCRIPT::execFuncCase160SetFlagman()
    {
        const int spriteHandle = popSpriteReferenceValue();
        const int playerIndex = popIntegerValue();
        MAP::Current()->SetFlagman(playerIndex, scriptResolveSpriteReference(spriteHandle));
        return 0;
    }

    int SCRIPT::execFuncCase162GetVidData()
    {
        const int type = popIntegerValue();
        const int nvid = popIntegerValue();

        VID* vid = resolveVidByNvid(nvid);
        if (vid == MAP::NullVid())
        {
            vid = MAP::NullVid();
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", "for GetVid", nvid);
        }
        if (vid == MAP::NullVid())
        {
            pushIntegerValue(0);
            return 0;
        }

        switch (static_cast<script::VidDataCode>(type))
        {
        case script::VidDataCode::MaxHp:
            pushIntegerResult(static_cast<int>(vid->maxHp));
            return 0;
        case script::VidDataCode::BattleRange:
            pushIntegerResult(static_cast<int>(vid->weaponBattleRange()));
            return 0;
        case script::VidDataCode::Ammo:
            pushIntegerResult(vid->activeWeaponAmmoCapacity());
            return 0;
        case script::VidDataCode::Name:
            pushStringResult(vid->scriptName());
            return 0;
        case script::VidDataCode::Count:
            pushIntegerResult(vid->totalSpriteCount());
            return 0;
        case script::VidDataCode::KilledUnit:
            pushIntegerResult(vid->totalKilledUnitCount());
            return 0;
        case script::VidDataCode::KilledUnitArmy0:
        case script::VidDataCode::KilledUnitArmy1:
        case script::VidDataCode::KilledUnitArmy2:
        case script::VidDataCode::KilledUnitArmy3:
            pushIntegerResult(vid->killedUnitCountForArmy(type - script::toInt(script::VidDataCode::KilledUnitArmy0)));
            return 0;
        case script::VidDataCode::CountArmy0:
        case script::VidDataCode::CountArmy1:
        case script::VidDataCode::CountArmy2:
        case script::VidDataCode::CountArmy3:
            pushIntegerResult(vid->spriteCountForBucket(type - script::toInt(script::VidDataCode::CountArmy0)));
            return 0;
        case script::VidDataCode::MaxHpArmy0:
        case script::VidDataCode::MaxHpArmy1:
        case script::VidDataCode::MaxHpArmy2:
        case script::VidDataCode::MaxHpArmy3:
            pushIntegerResult(vid->frameTimeForBucket(type - script::toInt(script::VidDataCode::MaxHpArmy0)));
            return 0;
        case script::VidDataCode::SpriteType:
            pushIntegerResult(static_cast<int>(vid->spriteTypeId()));
            return 0;
        case script::VidDataCode::Class:
            pushIntegerResult(static_cast<int>(vid->spriteClassId()));
            return 0;
        case script::VidDataCode::Speed:
            pushIntegerResult(vid->maxSpeedValue() == 999999.0f
                ? 999999
                : static_cast<int>(vid->maxSpeedValue() * 1000.0f));
            return 0;
        case script::VidDataCode::Lifetime:
            pushIntegerResult(vid->weaponLifetime());
            return 0;
        case script::VidDataCode::DetectRange:
            pushIntegerResult(static_cast<int>(vid->weaponDetectRange()));
            return 0;
        case script::VidDataCode::WeaponAim:
            pushIntegerResult(static_cast<int>(vid->weaponAim()));
            return 0;
        case script::VidDataCode::DirectionCount:
            pushIntegerResult(vid->directionCount());
            return 0;
        case script::VidDataCode::MoveMask:
            pushIntegerResult(static_cast<int>(vid->movementMask()));
            return 0;
        case script::VidDataCode::BuildTime:
            pushIntegerResult(vid->weaponBuildTime());
            return 0;
        case script::VidDataCode::Hide:
            pushIntegerResult(vid->hasPropertyBit400());
            return 0;
        case script::VidDataCode::NotCreateAsChild:
            pushIntegerResult(vid->notCreateAsChild());
            return 0;
        case script::VidDataCode::FrameSpeed:
            pushIntegerResult(static_cast<int>(vid->defaultFrameSpeed()));
            return 0;
        case script::VidDataCode::Link:
            pushIntegerResult(vid->linkedVid() ? vid->linkedVid()->nvid() : 0);
            return 0;
        case script::VidDataCode::Damage:
            pushIntegerResult(vid->deathDamageMinimumRawBits());
            return 0;
        case script::VidDataCode::RecolorUnit:
            pushIntegerResult(vid->totalRecolorUnitCount());
            return 0;
        case script::VidDataCode::RecolorUnitArmy0:
        case script::VidDataCode::RecolorUnitArmy1:
        case script::VidDataCode::RecolorUnitArmy2:
        case script::VidDataCode::RecolorUnitArmy3:
            pushIntegerResult(vid->recolorUnitCountForArmy(type - script::toInt(script::VidDataCode::RecolorUnitArmy0)));
            return 0;
        default:
            break;
        }

        return getVidDataFallback(vid, type);
    }

    int SCRIPT::pushZeroNativeResult()
    {
        pushIntegerResult(0);
        return 0;
    }

    int SCRIPT::getVidDataFallback(VID* vid, int type)
    {
        
        if (type >= script::VidChildFirst && type < script::VidChildEnd)
        {
            VID* child = vid->childVidForDataCode(type);
            if (!child)
                return pushZeroNativeResult();
            pushIntegerResult(child->nvid());
            return 0;
        }

        if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
        {
            pushIntegerResult(vid->noChildValueForDataCode(type));
            return 0;
        }

        reportScriptError(0x0E, "GetVid type", type);
        return 0;
    }

    int SCRIPT::execFuncCase163SetVidData()
    {
        const int value = popIntegerValue();
        const int type = popIntegerValue();
        VID* vid = popVidValue("for SetVid");
        if (vid == MAP::NullVid())
            return finishNativeWithoutResult();

        switch (static_cast<script::VidDataCode>(type))
        {
        case script::VidDataCode::MaxHp:
            vid->maxHp = value;
            return 0;
        case script::VidDataCode::Ammo:
        {
            VID* target = vid;
            VID* link = vid->linkedVid();
            if (link && link->CanFight() != 0)
                target = link;
            target->setWeaponRecordAmmoCapacity(value);
            return 0;
        }
        case script::VidDataCode::KilledUnit:
            vid->setKilledUnitCountForArmy(3, value);
            vid->setKilledUnitCountForArmy(2, value);
            vid->setKilledUnitCountForArmy(1, value);
            [[fallthrough]];
        case script::VidDataCode::KilledUnitArmy0:
            vid->setKilledUnitCountForArmy(0, value);
            return 0;
        case script::VidDataCode::KilledUnitArmy1:
            vid->setKilledUnitCountForArmy(1, value);
            return 0;
        case script::VidDataCode::KilledUnitArmy2:
            vid->setKilledUnitCountForArmy(2, value);
            return 0;
        case script::VidDataCode::KilledUnitArmy3:
            vid->setKilledUnitCountForArmy(3, value);
            return 0;
        case script::VidDataCode::MaxHpArmy0:
        case script::VidDataCode::MaxHpArmy1:
        case script::VidDataCode::MaxHpArmy2:
        case script::VidDataCode::MaxHpArmy3:
            vid->setBucketFrameTime(type - script::toInt(script::VidDataCode::MaxHpArmy0), value);
            return 0;
        case script::VidDataCode::HpCoeffArmy0:
        case script::VidDataCode::HpCoeffArmy1:
        case script::VidDataCode::HpCoeffArmy2:
        case script::VidDataCode::HpCoeffArmy3:
            vid->setBucketFramePercent(type - script::toInt(script::VidDataCode::HpCoeffArmy0), value);
            return 0;
        case script::VidDataCode::Speed:
            vid->setMaxSpeedValue(static_cast<float>(value) * 0.001f);
            return 0;
        case script::VidDataCode::Lifetime:
        {
            WEAPON* const currentWeapon = vid->weaponRecord();
            WEAPON* const sentinelWeapon = MAP::Current()->weaponTable();
            if (vid->nvid() == 0 || currentWeapon == sentinelWeapon)
                return finishNativeWithoutResult();
            vid->setWeaponLifetime(value);

            core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            const int slotCount = vidTable.count();
            if (slotCount <= 0)
                return finishNativeWithoutResult();

            for (int index = 0; index < slotCount; ++index)
            {
                VID* const candidate = vidTable.slot(index);
                if (candidate && candidate->weaponRecord() == vid->weaponRecord())
                    candidate->setActionAuxStateRequired(1);
            }
            return 0;
        }
        case script::VidDataCode::DetectRange:
            vid->setWeaponDetectRange(static_cast<float>(value));
            return 0;
        case script::VidDataCode::WeaponAim:
            vid->setWeaponAim(static_cast<float>(value));
            return 0;
        case script::VidDataCode::ExchangeVid:
            if (!isValidNvid(value))
            {
                reportScriptError(4, "SetVid get_image", value);
                return 0;
            }
            (void)MAP::Current()->swapVidReferences(vid, resolveVidByNvid(value));
            return 0;
        case script::VidDataCode::MoveMask:
            vid->setMovementMask(static_cast<DWORD>(value));
            return 0;
        case script::VidDataCode::BuildTime:
            vid->setWeaponBuildTime(value);
            return 0;
        case script::VidDataCode::Hide:
            vid->setLinkedPropertyBit400(value);
            return 0;
        case script::VidDataCode::NotCreateAsChild:
            vid->setNotCreateAsChild(value);
            return 0;
        case script::VidDataCode::FrameSpeed:
            vid->setDefaultFrameSpeed(static_cast<WORD>(value));
            return 0;
        case script::VidDataCode::Link:
        {
            vid->setLinkedVid(resolveVidByNvid(value));
            return 0;
        }
        case script::VidDataCode::Damage:
            vid->setDeathDamageMinimumRawBits(value);
            return 0;
        default:
            break;
        }

        return setVidDataFallback(vid, type, value);
    }

    int SCRIPT::setVidDataFallback(VID* vid, int type, int value)
    {
        
        if (!vid)
            return finishNativeWithoutResult();

        if (type >= script::VidChildFirst && type < script::VidChildEnd)
        {
            if (value == 0)
            {
                vid->setChildNvidForDataCode(type, 0);
                vid->setChildVidForDataCode(type, nullptr);
                return 0;
            }

            const int absValue = value < 0 ? -value : value;
            const bool validChildSlot = isValidNvid(absValue);
            if (!validChildSlot)
            {
                reportScriptError(4, "SetVid child", value);
                return 0;
            }
            
            VID* childForStore = resolveVidByNvid(absValue);
            vid->setChildNvidForDataCode(type, value);
            vid->setChildVidForDataCode(type, childForStore);
            VID* childForFlag = resolveVidByNvid(absValue);
            if (!childForFlag || childForFlag->PropBirthAsSmoke() == 0)
                return finishNativeWithoutResult();
            vid->setActionAuxStateRequired(vid->actionAuxStateRequired() | 1);
            return 0;
        }

        if (type >= script::toInt(script::VidDataCode::Gamma0) && type <= script::toInt(script::VidDataCode::Gamma3))
        {
            const GammaRawPair rawGamma = decodePackedGamma(value);
            vid->SetGammaRaw(rawGamma, static_cast<unsigned>(type - script::toInt(script::VidDataCode::Gamma0)));
            return 0;
        }

        if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
        {
            vid->setNoChildValueForDataCode(type, value);
            return 0;
        }

        reportScriptError(0x0E, "SetVid type", type);
        return 0;
    }


    int SCRIPT::execFuncCase164Itoa()
    {
        const int value = popIntegerValue();
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%d", value);
        pushStringResult(STRING(buffer));
        return 0;
    }


    int SCRIPT::execFuncCase165Sin()
    {
        const int angle = popIntegerValue();
        pushIntegerResult(scriptNativeSin1024(angle));
        return 0;
    }

    int SCRIPT::execFuncCase166Cos()
    {
        const int angle = popIntegerValue();
        pushIntegerResult(scriptNativeCos1024(angle));
        return 0;
    }

    int SCRIPT::execFuncCase167MapSizeX()
    {
#ifdef _WIN32
        const int value = static_cast<int>(win::applicationWinInstance()->mapExtentX());
#else
        const int value = static_cast<int>(core::ApplicationMapWidth());
#endif
        pushIntegerResult(value);
        return 0;
    }

    int SCRIPT::execFuncCase168MapSizeY()
    {
#ifdef _WIN32
        const int value = static_cast<int>(win::applicationWinInstance()->mapExtentY());
#else
        const int value = static_cast<int>(core::ApplicationMapHeight());
#endif
        pushIntegerResult(value);
        return 0;
    }




    int SCRIPT::execFuncCase172Printf()
    {
        if (topValueIsString())
        {
            const STRING value = popStringValue();
            STRING localValue;
            copyConstructString(localValue, value);

            const char* valueText = stringTextPointer(localValue);
            const STRING format = popStringValue();
            const char* formatText = stringTextPointer(format);

            const STRING formatted = STRING::Format(formatText, valueText);
            pushStringResult(formatted);
            return 0;
        }

        const int value = popIntegerValue();
        const STRING format = popStringValue();
        const char* formatText = stringTextPointer(format);
        const STRING formatted = STRING::Format(formatText, value);
        pushStringResult(formatted);
        return 0;
    }

    int SCRIPT::execFuncCase173ReloadVid()
    {
        
        (void)MAP::Current()->reloadGameResourceParameters();
        return 0;
    }


    int SCRIPT::execFuncCase174Fwrite()
    {
        const STRING value = popStringValue();
        const int fileValue = popIntegerValue();
        if (fileValue == 0)
            return finishNativeWithoutResult();

        std::FILE* file = scriptNativeFileFromInt(fileValue);
        if (!file)
            return 0;

        writeCStringRecord(value, file);
        std::fseek(file, -1, SEEK_CUR);
        std::fputs("\n", file);
        return 0;
    }

    int SCRIPT::execFuncCase175Fread()
    {
        const int fileValue = popIntegerValue();
        STRING lpFile;
        RESOURCE& demoResource = MAP::Current()->demoResource();

        if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
        {
            readStringLineFromStream(lpFile, &demoResource);
        }
        else if (fileValue != 0)
        {
            readStringLineFromFile(lpFile, scriptNativeFileFromInt(fileValue));
        }

        if ((core::ApplicationFlags() & application_flags::DemoWriteToResource) != 0)
            writeCStringToStream(lpFile, &demoResource);

        pushStringResult(lpFile);
        return 0;
    }

    int SCRIPT::execFuncCase176Fopen()
    {
        const STRING filename = popStringValue();
        if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
        {
            pushIntegerResult(0);
            return 0;
        }

        std::FILE* file = openScriptFile(filename, "r+t");
        if (!file)
            LOG::ResourceError("SCRIPT", 7, stringTextPointer(filename), 0);
        pushIntegerResult(scriptNativeIntFromFile(file));
        return 0;
    }

    int SCRIPT::execFuncCase177Fclose()
    {
        const int fileValue = popIntegerValue();
        if (fileValue == 0)
            return finishNativeWithoutResult();
        if (std::FILE* file = scriptNativeFileFromInt(fileValue))
            std::fclose(file);
        return 0;
    }

    int SCRIPT::execFuncCase178Fcreate()
    {
        const STRING filename = popStringValue();
        if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
            return pushZeroNativeResult();

        std::FILE* file = openScriptFile(filename, "w+t");
        pushIntegerResult(scriptNativeIntFromFile(file));
        return 0;
    }

    int SCRIPT::execFuncCase179Feof()
    {
        const int fileValue = popIntegerValue();
        if (fileValue == 0)
            return pushTrueNativeResult();

        std::FILE* file = scriptNativeFileFromInt(fileValue);
        pushIntegerResult(file && std::feof(file) ? 0x10 : 0);
        return 0;
    }


    int SCRIPT::execFuncCase182GetReg()
    {
        const STRING defaultValue = popStringValue();
        const STRING name = popStringValue();
        const STRING path = popStringValue();
        const STRING value = path.ReadRegistryString(name, defaultValue);
        pushStringResult(value);
        return 0;
    }

    int SCRIPT::execFuncCase183SetReg()
    {
        const STRING value = popStringValue();
        const STRING name = popStringValue();
        const STRING path = popStringValue();
        path.WriteRegistryString(name, value);
        return 0;
    }

    int SCRIPT::execFuncCase184DelReg()
    {
        const STRING name = popStringValue();
        const STRING path = popStringValue();
        path.DeleteRegistryValue(name);
        return 0;
    }

    int SCRIPT::execFuncCase185GetDefaultRegPath()
    {
        pushStringResult(core::StartupRegistryPath());
        return 0;
    }

    int SCRIPT::execFuncCase70Handler(core::ApplicationDrawDispatcherState& drawState)
    {
        int index = m_physical.stackCount - 1;
        m_physical.stackCount = index;
        if (index < 0 || index >= m_physical.stackCapacity ||
            static_cast<std::size_t>(index) >= host().m_executionStack.size())
            return 0;

        const script::StackObject& entry = host().m_executionStack[static_cast<std::size_t>(index)];
        const int value = (entry.flags & script::STACK_OBJECT_STRING)
            ? script::ParseStackIntegerText(entry.text.c_str())
            : entry.intValue;

        if (value != 0)
        {
            core::Application::beginBucketTimingSnapshot(drawState);
            if (mouseInstanceRef())
                mouseInstanceRef()->setCursorId(0);
        }
        else
        {
            core::Application::endBucketTimingSnapshot(drawState);
        }

        return 0;
    }
}
