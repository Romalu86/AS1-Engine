#pragma once
#include <cstdint>

namespace as1::script
{
    // Retail Alien Shooter 1 SCRIPT external/native function IDs.
    // Centralizing the ABI values keeps dispatcher code readable while the
    // numeric contract remains explicit in one place.
    enum class NativeFunctionCode : std::int32_t
    {
        CreateSprite = 65, Flagman = 66, FirstUnit = 68, NextUnit = 69, GetSprite = 70, GetSpriteScr = 71, FindNearestSprite = 72,
        FirstInBox = 74, NextInBox = 75, FirstSprite = 76, NextSprite = 77, Action = 79, SizeTo = 80, AddCommand = 82, GetUnitVid = 83,
        Destroy = 84, GetX = 85, GetY = 86, GetZ = 87, GetDirection = 88, GetAnimation = 89, DirectionTo = 90, GetCommands = 96,
        SetCommands = 97, Load = 98, Save = 99, SaveDemo = 100, MenuFind = 101, MenuLoad = 102, MenuRelease = 103, MenuNvidUnderCursor = 104,
        MenuNdirUnderCursor = 105, MenuAction = 106, MenuCreate = 107, MenuLeftClick = 108, GetInputX = 109, GetInputY = 110, GetKey = 111,
        LegacyHandler = 112, SetCursor = 113, MessageText = 114, GetInputState = 115, SetShiftCoor = 116, SetScrollType = 117,
        GetScrollType = 118, ScreenX = 119, ScreenY = 120, SetApplicationFlag7 = 121, PlayerNoop = 122, GetString = 123, Exit = 124,
        ToScreenX = 125, ToScreenY = 126, MenuRightClick = 127, SetMouseClick = 128, SetSoundVolume = 130, SetMusicVolume = 131, PlaySfx = 132,
        StopSfx = 133, StopMusic = 134, PlaySfxFromCoor = 135, PlayMusicFile = 136, Effect = 137, SetEnvironment = 138, SetGraphDetail = 139,
        SetGamma = 140, SetWind = 141, PlayMovie = 142, IsPlayMovie = 143, StopMovie = 144, IsPlayMusic = 145, CountGamma = 146, GetGamma = 147,
        GetEffectState = 148, ExecuteShellFile = 152, CharAt = 153, Log = 154, Random = 155, ChangeZUnit = 156, GetTime = 157, GetGroundZ = 158,
        StringLength = 159, SetFlagman = 160, AskPlace = 161, GetVidData = 162, SetVidData = 163, IntToString = 164, Sin = 165, Cos = 166,
        MapSizeX = 167, MapSizeY = 168, Genocide = 169, ReplaceUnit = 170, Printf = 172, ReloadVid = 173, FileWrite = 174, FileRead = 175,
        FileOpen = 176, FileClose = 177, FileCreate = 178, FileEof = 179, RegistryGet = 182, RegistrySet = 183, RegistryDelete = 184,
        RegistryDefaultPath = 185, TrainProperty = 212, SetSemaphore = 231, BreakTrain = 239, FirstTrain = 240, NextTrain = 241, PatrolEngine = 242,
        SetPushLine = 243, GetScreenInputX = 244, GetScreenInputY = 245, PlayerPathFlag = 246, SetAutoReBirth = 247, AddUnitLimit = 249,
        SetEnemyCanAttackNeutralTrains = 250, SetMoney = 251, GetMoney = 252, Noop253 = 253, Noop254 = 254,
    };
}
