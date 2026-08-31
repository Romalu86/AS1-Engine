#include "lgc_script.h"
#include <map>
#include <memory>
#include <cstdint>
#include <cstring>

namespace as1
{
    namespace
    {
        std::map<const SCRIPT*, std::unique_ptr<ScriptHostState>>& scriptHostStates()
        {
            static auto* states = new std::map<const SCRIPT*, std::unique_ptr<ScriptHostState>>();
            return *states;
        }

        struct ScriptHostLookupCache
        {
            const SCRIPT* owner = nullptr;
            ScriptHostState* state = nullptr;
        };

        ScriptHostLookupCache& scriptHostLookupCache() noexcept
        {
            static ScriptHostLookupCache cache;
            return cache;
        }

        ScriptHostState& ensureScriptHostState(const SCRIPT* owner)
        {
            ScriptHostLookupCache& cache = scriptHostLookupCache();
            if (cache.owner == owner && cache.state != nullptr)
                return *cache.state;

            auto& states = scriptHostStates();
            auto it = states.find(owner);
            if (it == states.end())
                it = states.emplace(owner, std::make_unique<ScriptHostState>()).first;
            cache.owner = owner;
            cache.state = it->second.get();
            return *cache.state;
        }

        ScriptHostState* findScriptHostState(const SCRIPT* owner) noexcept
        {
            ScriptHostLookupCache& cache = scriptHostLookupCache();
            if (cache.owner == owner)
                return cache.state;

            auto& states = scriptHostStates();
            const auto it = states.find(owner);
            ScriptHostState* const state = it == states.end() ? nullptr : it->second.get();
            cache.owner = owner;
            cache.state = state;
            return state;
        }

        void eraseScriptHostState(const SCRIPT* owner) noexcept
        {
            ScriptHostLookupCache& cache = scriptHostLookupCache();
            if (cache.owner == owner)
            {
                cache.owner = nullptr;
                cache.state = nullptr;
            }
            scriptHostStates().erase(owner);
        }

#if defined(_MSC_VER) && defined(_M_IX86)
        using ScriptListDeletingDestructor = void* (__fastcall*)(void*, void*, unsigned char);
        void* __fastcall scriptStackListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);
        void* __fastcall scriptFunctionListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);
        void* __fastcall scriptDefineListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);

        ScriptListDeletingDestructor scriptStackListVtable[] = { &scriptStackListDeletingDestructor };
        ScriptListDeletingDestructor scriptFunctionListInitialVtable[] = { &scriptFunctionListDeletingDestructor };
        ScriptListDeletingDestructor scriptDefineListInitialVtable[] = { &scriptDefineListDeletingDestructor };
        ScriptListDeletingDestructor scriptFunctionListFinalVtable[] = { &scriptFunctionListDeletingDestructor };
        ScriptListDeletingDestructor scriptDefineListFinalVtable[] = { &scriptDefineListDeletingDestructor };

        std::uint32_t currentPointerToken(const void* pointer) noexcept
        {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
        }

        void destroyScriptFunctionListSubobject(void* rawThis) noexcept
        {
            auto* owner = reinterpret_cast<SCRIPT*>(static_cast<unsigned char*>(rawThis) - 0x10u);
            auto* physical = reinterpret_cast<ScriptPhysicalLayout*>(owner);
            physical->functionListVtable = currentPointerToken(scriptFunctionListFinalVtable);
            if (ScriptHostState* h = findScriptHostState(owner))
                h->m_functionTable.clearRecords();
            physical->functionTableToken = 0u;
            physical->functionCount = 0;
        }

        void destroyScriptDefineListSubobject(void* rawThis) noexcept
        {
            auto* owner = reinterpret_cast<SCRIPT*>(static_cast<unsigned char*>(rawThis) - 0x20u);
            auto* physical = reinterpret_cast<ScriptPhysicalLayout*>(owner);
            physical->defineListVtable = currentPointerToken(scriptDefineListFinalVtable);
            if (ScriptHostState* h = findScriptHostState(owner))
            {
                // RetailRawArray::clear() is the Win32/x86 physical [data-4]
                // release route and destroys records in reverse order.
                h->m_defines.clear();
            }
            physical->defineTableToken = 0u;
            physical->defineCount = 0;
        }

        void* __fastcall scriptStackListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            auto* owner = reinterpret_cast<SCRIPT*>(rawThis);
            auto* physical = reinterpret_cast<ScriptPhysicalLayout*>(owner);
            physical->stackListVtable = currentPointerToken(scriptStackListVtable);
            if (ScriptHostState* h = findScriptHostState(owner))
                h->m_executionStack.clear();
            // Retail clears only table (+0x0C) and logical count (+0x04).
            // Capacity (+0x08) is intentionally left unchanged.
            physical->stackTableToken = 0;
            physical->stackCount = 0;
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }

        void* __fastcall scriptFunctionListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            // Deleting-destructor sibling of destroyScriptFunctionListSubobject: identical base
            // teardown plus the flag&1 scalar delete tail.
            destroyScriptFunctionListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }

        void* __fastcall scriptDefineListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            // Deleting-destructor sibling of destroyScriptDefineListSubobject.
            destroyScriptDefineListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }
#endif

        void* initializeScriptFunctionListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[1] = 0u;
            slot[2] = 0u;
#if defined(_MSC_VER) && defined(_M_IX86)
            slot[0] = currentPointerToken(scriptFunctionListInitialVtable);
#else
            slot[0] = 0u;
#endif
            return rawThis;
        }

        void* initializeScriptDefineListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[1] = 0u;
            slot[2] = 0u;
#if defined(_MSC_VER) && defined(_M_IX86)
            slot[0] = currentPointerToken(scriptDefineListInitialVtable);
#else
            slot[0] = 0u;
#endif
            return rawThis;
        }

        void* initializeScriptStackListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
#if defined(_MSC_VER) && defined(_M_IX86)
            slot[0] = currentPointerToken(scriptStackListVtable);
#else
            slot[0] = 0u;
#endif
            slot[1] = 0u;
            slot[2] = 0u;
            return rawThis;
        }
    }

    SCRIPT::SCRIPT()
    {
        
        std::memset(&m_physical, 0, sizeof(m_physical));
        initializeScriptStackListSubobject(&m_physical.stackListVtable);
        initializeScriptFunctionListSubobject(&m_physical.functionListVtable);
        initializeScriptDefineListSubobject(&m_physical.defineListVtable);
        m_physical.scriptFileToken = retailPointerToken(as1::STRING::SharedEmptyText());
        m_physical.sourceLine = -1;
    }

    ScriptHostState& SCRIPT::host() noexcept
    {
        return ensureScriptHostState(this);
    }

    const ScriptHostState& SCRIPT::host() const noexcept
    {
        return ensureScriptHostState(this);
    }

    std::uint32_t SCRIPT::retailPointerToken(const void* pointer) noexcept
    {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) & 0xFFFFFFFFu);
    }

    void SCRIPT::syncPhysicalFunctionList() noexcept
    {
        const auto& table = host().m_functionTable;
        m_physical.functionCount = table.count();
        m_physical.functionCapacity = table.capacity();
        const auto& items = table.items();
        m_physical.functionTableToken = items.empty() ? 0u : retailPointerToken(items.data());
    }

    void SCRIPT::syncPhysicalDefineList() noexcept
    {
        const auto& items = host().m_defines;
        m_physical.defineCount = static_cast<std::int32_t>(items.size());
        m_physical.defineTableToken = items.empty() ? 0u : retailPointerToken(items.data());
    }

    void SCRIPT::syncPhysicalSourcePointers() noexcept
    {
        auto& source = host().m_sourceBuffer;
        if (source.empty())
        {
            
            m_physical.sourceBufferToken = 0;
            m_physical.sourceCursor = 0;
            return;
        }

        const int cursor = host().m_portableSourceCursorOffset;
        const int end = host().m_portableSourceEndOffset;
        const std::uint8_t* const base = source.data();
        m_physical.sourceBufferToken = retailPointerToken(base);
        m_physical.sourceCursor = retailPointerToken(base + (cursor < 0 ? 0 : cursor));
        m_physical.sourceEnd = retailPointerToken(base + (end < 0 ? 0 : end));
    }

    void SCRIPT::syncPhysicalBackingPointers() noexcept
    {
        auto& h = host();
        m_physical.stackTableToken = h.m_executionStack.empty()
            ? 0u : retailPointerToken(h.m_executionStack.data());
        m_physical.scriptFileToken = retailPointerToken(h.m_scriptFile.c_str());
        m_physical.bytecodeBufferToken = !h.m_bytecode.empty()
            ? retailPointerToken(h.m_bytecode.data())
            : (h.m_zeroLengthBytecodeAllocation
                ? retailPointerToken(h.m_zeroLengthBytecodeAllocation) : 0u);
        syncPhysicalFunctionList();
        syncPhysicalDefineList();
        syncPhysicalSourcePointers();
    }

    SCRIPT::~SCRIPT()
    {
        
        if (findScriptHostState(this))
            resetScriptVmState();
        else
        {
            m_physical.stackCount = 0;
            m_physical.stackCapacity = 0;
            m_physical.stackTableToken = 0;
            m_physical.functionCount = 0;
            m_physical.functionCapacity = 0;
            m_physical.functionTableToken = 0;
            m_physical.defineCount = 0;
            m_physical.defineCapacity = 0;
            m_physical.defineTableToken = 0;
            m_physical.bytecodeBufferToken = 0;
            m_physical.bytecodeEnd = 0;
            m_physical.sourceBufferToken = 0;
            m_physical.sourceCursor = 0;
            m_physical.conditionalDepth = 0;
            m_physical.fallbackFunction = -1;
            m_physical.parseMode = 0;
        }
#if defined(_MSC_VER) && defined(_M_IX86)
        destroyScriptDefineListSubobject(&m_physical.defineListVtable);
        destroyScriptFunctionListSubobject(&m_physical.functionListVtable);
        m_physical.stackListVtable = currentPointerToken(scriptStackListVtable);
        m_physical.stackTableToken = 0u;
        m_physical.stackCount = 0;
#endif
        eraseScriptHostState(this);
    }

    void SCRIPT::SetNativeContext(const ScriptNativeContext& context)
    {
        host().m_nativeContext = context;
    }
}
