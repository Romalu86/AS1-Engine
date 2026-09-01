#pragma once
#include "../core/as_string.h"
#include "../graphics/gamma.h"
#include "logic_runtime.h"
#include "retail_raw_array.h"
#include "retail_byte_buffer.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <array>

namespace as1
{
    class VID;
    class SPRITE;
    struct WEAPON;
    namespace core { struct ApplicationDrawDispatcherState; }
    struct ScriptDefinePairRecord
    {
        STRING name;
        STRING value;
    };

    // Retail 8-byte define-pair deleting/destructor owners.
    // owners used by SCRIPT list teardown.
    void* scriptDefinePairDeletingDestructor(ScriptDefinePairRecord* self, unsigned char flags) noexcept;
    void destroyScriptDefinePair(ScriptDefinePairRecord* self) noexcept;

    struct ScriptPhysicalLayout
    {
        std::uint32_t stackListVtable = 0;
        std::int32_t stackCount = 0;
        std::int32_t stackCapacity = 0;
        std::uint32_t stackTableToken = 0;
        std::uint32_t functionListVtable = 0;
        std::int32_t functionCount = 0;
        std::int32_t functionCapacity = 0;
        std::uint32_t functionTableToken = 0;
        std::uint32_t defineListVtable = 0;
        std::int32_t defineCount = 0;
        std::int32_t defineCapacity = 0;
        std::uint32_t defineTableToken = 0;
        std::uint32_t scriptFileToken = 0;
        std::uint32_t bytecodeBufferToken = 0;
        std::int32_t bytecodeEnd = 0;
        std::uint32_t sourceCursor = 0;
        std::uint32_t sourceEnd = 0;
        std::uint32_t sourceBufferToken = 0;
        std::int32_t sourceLine = -1;
        std::int32_t conditionalDepth = 0;
        std::int32_t fallbackFunction = -1;
        std::int32_t parseMode = 0;
    };

#if defined(_MSC_VER) && defined(_M_IX86)
#endif


    struct ScriptNativeContext
    {
        // Split-owner bridges retained only where the unified retail
        // Application physical layout is not yet assembled.
        std::function<void(const STRING& path)> queueMapLoadSlot18Flag40;
    };



    // Host/tooling state is deliberately external to the retail SCRIPT object.
    // Retail SCRIPT is exactly 0x58 bytes embedded at Application+0x14C.
    struct ScriptHostState
    {
        
        script::RetailRawArray<script::StackObject> m_executionStack;
        script::LogicFunctionList m_functionTable;
        script::RetailRawArray<ScriptDefinePairRecord> m_defines;
        STRING m_scriptFile;
        script::RetailByteBuffer m_bytecode;
        void* m_zeroLengthBytecodeAllocation = nullptr;
        script::RetailByteBuffer m_sourceBuffer;
        // Portable compiler cursor offsets are translation state for the 64-bit
        // host build. On Win32/x86 syncPhysicalSourcePointers() publishes the
        // corresponding retail pointers at SCRIPT+0x3C/+0x40.
        int m_portableSourceCursorOffset = 0;
        int m_portableSourceEndOffset = 0;
        ScriptNativeContext m_nativeContext;
        // SCRIPT-native iterator cursors are retail process globals and live in
        // script_exec.cpp, not in this per-SCRIPT host representation.
    };

    class SCRIPT
    {
    public:
        SCRIPT();
        ~SCRIPT();
        SCRIPT(const SCRIPT&) = delete;
        SCRIPT& operator=(const SCRIPT&) = delete;

        int compileScriptSourceFile(const STRING& scriptFile, const STRING& gameRoot);
        int loadScriptFile(const STRING& scriptFile, const STRING& gameRoot);
        int writeExecutionStackToStream(BaseStream* stream);
        int readExecutionStackFromStream(BaseStream* stream);
        bool isLoaded() const { return m_physical.bytecodeEnd != 0; }

        void SetNativeContext(const ScriptNativeContext& context);

        static constexpr std::uint32_t InitialListCapacity = 0x80u;
        static constexpr std::uint32_t TemporaryBytecodeCapacity = 0x3E800u;
        static constexpr std::uint32_t SourceBufferPadding = 0x1000u;
        static constexpr std::uint32_t SourcePayloadOffset = 0x0FE2u;
        void clearExecutionStack();
        void pushExecutionInt(int value);
        void pushExecutionString(const STRING& value);
        int executionStackCount() const;
        int executionStackCapacity() const;
        const script::StackObject* executionStackAt(int index) const;
        script::StackObject* mutableExecutionStackAt(int index);
        script::StackObject* mutableExecutionStackStorageAt(int index);
        void appendExecutionStackObject(const script::StackObject& value);
        int clearSpriteReferencesFromExecutionStack(SPRITE* sprite);
        void growExecutionStackForAppend();
        void appendExecutionStackRecord(std::uint8_t flags, int value, const STRING& text);
        int functionCount() const;
        int functionCapacity() const;
        const script::LogicFunctionList& functionTable() const;
        int defineCount() const;
        int defineCapacity() const;
        const script::RetailRawArray<ScriptDefinePairRecord>& defineTable() const;
        const STRING& scriptFile() const;
        const script::RetailByteBuffer& bytecodeBuffer() const;
        int bytecodeEnd() const;
        int sourceCursorOffset() const;
        int sourceEndOffset() const;
        const script::RetailByteBuffer& sourceBuffer() const;
        int sourceLineNumber() const;
        int conditionalDepth() const;
        int fallbackFunctionIndex() const;
        int parseMode() const;
        std::uint8_t sourceByteAtCursor() const;
        void setSourceCursorOffset(int offset);
        void reportCompileError(int errorCode, const char* detailText, int detailValue);
        int skipTriviaAndPreprocess();
        int requireSourceToken();
        int readSourceLine(STRING& outLine);
        int readIdentifier(STRING& outName);
        int matchToken(const char* token);
        int requireToken(const char* token);
        int parseConstantIntExpression();
        int readQuotedStringLiteral(char* outText);
        int setLastFunctionElementCount(int argCount);
        void compileIntDeclaration();
        void compileStringDeclaration();
        int compilePrimaryExpression();
        void emitOrFoldBinaryCommand(int bytecodeStart, int opcode);
        void compileMultiplicativeExpression();
        void compileAdditiveExpression();
        void compileComparisonExpression();
        void compileExpression();
        void compileExpressionList();
        void compileStatement(std::int32_t* breakPatchList);
        int compileNextSourceItem();
        int compileDefineDirective();
        int compileUndefDirective();
        int compileIncludeDirective();
        int compileExternDirective();
        int compileFunctionDirective();
        void resetScriptVmState();
        void prepareSourceCompiler(const STRING& scriptFile, BaseStream* stream, std::uint32_t sourceSize);
        void clearFunctionTable();
        void appendFunctionRecord(const STRING& name, std::uint8_t flags, const STRING& text, int bytecodeStart0C, int stackBase10, int argCount14);
        void clearDefines();
        int findDefine(const STRING& name) const;
        int addOrReplaceDefine(const STRING& name, const STRING& value);
        int undefine(const STRING& name);
        int rewriteDefineMacro(int tokenStartOffset, int tokenLength);
        int dispatchNativeFunction(int opcode);
        // Compatibility alias: MAP::ExecFunc(int).
        int executeNativeFunction(int opcode, core::ApplicationDrawDispatcherState& drawState);
        int execFuncCase65CreateSprite();
        int execFuncCase98Load();
        int execFuncCase101MenuFind();
        int execFuncCase102MenuLoad();
        int execFuncCase103MenuRelease();
        int execFuncCase104MenuNvidUnderCursor();
        int execFuncCase105MenuNdirUnderCursor();
        int execFuncCase106MenuAction();
        int execFuncCase124Exit();
        int execFuncCase161AskPlace();
        int execFuncCase169Genocide(core::ApplicationDrawDispatcherState& drawState);
        int execFuncCase66Flagman();
        int execFuncCase68FirstUnit();
        int execFuncCase69NextUnit();
        int execFuncCase70GetSprite();
        int execFuncCase71GetSpriteScr();
        int execFuncCase72FindNearestSprite();
        int execFuncCase74FirstInBox();
        int execFuncCase75NextInBox();
        int execFuncCase76FirstSprite(core::ApplicationDrawDispatcherState& drawState);
        int execFuncCase77NextSprite(core::ApplicationDrawDispatcherState& drawState);
        int execFuncCase79Action();
        int execFuncCase82AddCommand();
        int execFuncCase96GetCommands();
        int finishGetCommandsResult();
        int execFuncCase97SetCommands();
        int execFuncCase100SaveDemo();
        int pushTrueNativeResult();
        int finishNativeWithoutResult();
        int execFuncCase83GetUnitVid();
        int execFuncCase84Destroy();
        int execFuncCase85GetX();
        int execFuncCase86GetY();
        int execFuncCase87GetZ();
        int execFuncCase88GetDirection();
        int execFuncCase89GetAnimation();
        int execFuncCase80SizeTo();
        int execFuncCase90DirectionTo();
        int execFuncCase119ScreenX();
        int execFuncCase120ScreenY();
        int execFuncCase123GetString();
        int execFuncCase125ToScreenX();
        int execFuncCase126ToScreenY();
        int execFuncCase108MenuLclick();
        int execFuncCase109GetInputX();
        int execFuncCase110GetInputY();
        int execFuncCase111GetKey();
        int execFuncCase115GetInputState();
        int execFuncCase117SetScrollType();
        int execFuncCase118GetScrollType();
        int execFuncCase127MenuRclick();
        int execFuncCase244GetScreenInputX();
        int execFuncCase245GetScreenInputY();
        int execFuncCase113SetCursor();
        int execFuncCase128SetMouseClick();
        int execFuncCase130SetSoundVolume();
        int execFuncCase131SetMusicVolume();
        int execFuncCase132PlaySfx();
        int execFuncCase133StopSfx();
        int execFuncCase134StopMusic();
        int execFuncCase135PlaySfxFromCoor();
        int execFuncCase136PlayMusicFile();
        int execFuncCase137Effect();
        int execFuncCase138SetEnvironment();
        int execFuncCase139SetGraphDetail();
        int execFuncCase140SetGamma();
        int execFuncCase141SetWind();
        int execFuncCase146CountGamma();
        int execFuncCase147GetGamma();
        int execFuncCase148GetEffectState();
        int execFuncCase142PlayMovie();
        int execFuncCase143IsPlayMovie();
        int execFuncCase144StopMovie();
        int execFuncCase145IsPlayMusic();
        int execFuncCase247SetAutoReBirth();
        int execFuncCase250SetEnemyCanAttackNeutralTrains();
        int execFuncCase153Charat();
        int execFuncCase154Log();
        int execFuncCase155Random();
        int execFuncCase157GetTime();
        int execFuncCase158GetGroundZ();
        int execFuncCase159Strlen();
        int execFuncCase160SetFlagman();
        int execFuncCase162GetVidData();
        int pushZeroNativeResult();
        int getVidDataFallback(VID* vid, int type);
        int execFuncCase163SetVidData();
        int setVidDataFallback(VID* vid, int type, int value);
        int execFuncCase164Itoa();
        int execFuncCase165Sin();
        int execFuncCase166Cos();
        int execFuncCase167MapSizeX();
        int execFuncCase168MapSizeY();
        int execFuncCase172Printf();
        int execFuncCase173ReloadVid();
        int execFuncCase174Fwrite();
        int execFuncCase175Fread();
        int execFuncCase176Fopen();
        int execFuncCase177Fclose();
        int execFuncCase178Fcreate();
        int execFuncCase179Feof();
        int execFuncCase182GetReg();
        int execFuncCase183SetReg();
        int execFuncCase184DelReg();
        int execFuncCase185GetDefaultRegPath();
        int execFuncCase99Save();
        int execFuncCase107MenuCreate();
        int execFuncCase114MessageText();
        int execFuncCase116SetShiftCoor();
        int execFuncCase121SetApplicationFlag7();
        int execFuncCase122PlayerNoop();
        int execFuncCase152ShellExecute();
        int execFuncCase156ChangeZUnit(core::ApplicationDrawDispatcherState& drawState);
        int execFuncCase170ReplaceUnit(core::ApplicationDrawDispatcherState& drawState);
        int execFuncCase212TrainProperty();
        int execFuncCase231SetSemaphore();
        int execFuncCase239BreakTrain();
        int execFuncCase240FirstTrain();
        int execFuncCase241NextTrain();
        int execFuncCase242PatrolEngine();
        int execFuncCase243SetPushLine();
        int execFuncCase246PlayerPathFlag();
        int execFuncCase249AddUnitLimit();
        int execFuncCase251SetMoney();
        int execFuncCase252GetMoney();
        int execFuncCase253Noop();
        int execFuncCase254Noop();
        int execFuncCase70Handler(core::ApplicationDrawDispatcherState& drawState);
        int popSpriteReferenceValue();
        SPRITE* popSpriteReference();
        void pushSpriteReference(SPRITE* sprite);
        void pushIntegerValue(int value);
        void pushSpriteReferenceValue(int value);
        void pushStringValue(const STRING& value);
        int popIntegerValue();
        GammaRawPair decodePackedGamma(int value);
        as1::VID* popVidValue(const char* errorContext);
        void reportScriptError(int errorCode, const char* text, int value);
        const char* stringTextPointer(const STRING& value) const;
        int writeCStringToStream(const STRING& source, BaseStream* target) const;
        std::size_t writeCStringRecord(const STRING& value, std::FILE* file) const;
        std::FILE* openScriptFile(const STRING& path, const char* mode) const;
        int topValueIsString() const;
        STRING popStringValue();
        const STRING& normalizeStackValueToString(script::StackObject& value);
        void pushStringResult(const STRING& value);
        void pushIntegerResult(int value);

        
        STRING getVariableString(const STRING& expression);
        // Compatibility alias: SCRIPT::callFunction.
        int callFunction(int functionIndex, int arg3, int arg4);

    private:
        ScriptHostState& host() noexcept;
        const ScriptHostState& host() const noexcept;
        static std::uint32_t retailPointerToken(const void* pointer) noexcept;
        void syncPhysicalBackingPointers() noexcept;
        void syncPhysicalFunctionList() noexcept;
        void syncPhysicalDefineList() noexcept;
        void syncPhysicalSourcePointers() noexcept;
        ScriptPhysicalLayout m_physical{};

    };


}
