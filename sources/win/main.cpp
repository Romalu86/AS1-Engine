#include "game/startup.h"
#include "core/configuration.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/types.h"
#include "core/application.h"
#include "base_sprite_list.h"
#include "sprite.h"
#include "graph.h"
#include "map.h"
#include "constant.h"
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <new>
#include <cstdint>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include "win/resources/resource.h"
#include "win/main_sw.h"
#include "win/application_win.h"
#include "input.h"
#include "input/control_actions.h"
#include "mouse.h"
#include "sound/engine.h"
#endif

namespace
{

    std::string joinedCommandLine(int argc, char** argv)
    {
        std::string out;
        for (int i = 1; i < argc; ++i)
        {
            if (!argv[i])
                continue;
            if (!out.empty())
                out += ' ';
            out += argv[i];
        }
        return out;
    }

    int runCommandLineStartup(int argc, char** argv, bool allowNoArgs)
    {
        if (!allowNoArgs && argc <= 1)
            return 0;
        // The non-Win research host publishes a retail-shaped executable
        // name so the same backslash/dot parsing path can be exercised.
        static char portableProgramPath[] = "AlienShooter.exe";
        as1::BindRetailProgramPathOwner(portableProgramPath);
        as1::InitializeGlobalFileLoggerOwner(true);
        const as1::STRING commandLine(joinedCommandLine(argc, argv));
        as1::core::StartupConfiguration config = as1::core::Configuration::LoadStartupConfiguration(as1::STRING("AlienShooter"),
                                                                                                     commandLine,
                                                                                                     as1::STRING("."));
        // Portable research host has no COM requirement, but still enters the
        // same post-COM profile-owner phase before consuming configuration.
        as1::core::initializePostComProfileOwners(config);
        as1::core::readStartupResourceProfileBlock(config);
        as1::core::readStartupStartMapProfileBlock(config, commandLine);
        as1::StartupOptions options = as1::core::Configuration::BuildStartupOptions(config);
        int result = 0;
        try
        {
            as1::GRAPH graph;
            as1::MAP map(&graph);
            std::filesystem::path root(options.resourceRoot.c_str() ? options.resourceRoot.c_str() : ".");
            if (root.empty())
                root = ".";
            root = root.lexically_normal();
            map.setResourceRoot(as1::STRING(root.string()));
            map.setObjectsResource(options.objectsResource);

            if (options.loadGameResources)
            {
                const std::filesystem::path objectsRes = map.resolveGameFile(options.objectsResource);
                as1::RESOURCE objectsResource;
                if (!std::filesystem::exists(objectsRes) ||
                    !objectsResource.openFile(as1::STRING(objectsRes.string()), as1::RESOURCE::ResTypes::DATA))
                {
                    result = 2;
                }
                else
                {
                    map.LoadSfx(&objectsResource);
                    map.LoadConstants(&objectsResource);
                    if (!map.loadGameResourcesFromResource(&objectsResource, false, false))
                        result = 2;
                }
            }

            if (result == 0 && options.loadMap)
            {
                const std::filesystem::path mapFile = map.resolveGameFile(options.mapName);
                if (!std::filesystem::exists(mapFile))
                    result = 2;
                else
                    map.load(options.mapName);
            }
        }
        catch (...)
        {
            result = 2;
        }
        as1::core::ReleaseStartupStringsIniPathOwner();
        as1::core::ReleaseStartupRegistryPathOwner();
        as1::ReleaseGlobalFileLoggerOwner();
        return result;
    }

#ifdef _WIN32

    int runWin32Application(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCmd)
    {
        // Retail CRT has already published g_executablePath before WinMain enters
        // the Application constructor chain.  Preserve that process owner, then
        // allocate exactly one 0x22A0 ApplicationWin object.
        as1::BindRetailProgramPathOwnerFromCrt();

        as1::win::ApplicationWinInit shellInit{};
        shellInit.hInstance = instance;
        shellInit.previousInstance = previousInstance;
        shellInit.commandLine = commandLine ? commandLine : "";
        shellInit.showCmd = showCmd;
        as1::win::ApplicationWin* applicationShell = as1::win::CreateApplicationWin(shellInit);
        if (!applicationShell)
            return 0;

        char* commandLineStorage = as1::STRING::SharedEmptyText();
        if (commandLine && commandLine[0] != '\0')
        {
            const std::size_t length = std::strlen(commandLine);
            commandLineStorage = static_cast<char*>(::operator new(length + 1u));
            std::memcpy(commandLineStorage, commandLine, length);
            commandLineStorage[length] = '\0';
        }
        const char* commandLineOwner = commandLineStorage;

        as1::win::ApplicationWin* const returnedOwner =
            applicationShell->initializeDerivedApplicationStartup(instance,
                                         previousInstance,
                                         &commandLineOwner,
                                         showCmd,
                                         &as1::core::StartupSettings());

        as1::core::BindApplicationPhysicalOwner(returnedOwner);

        if (commandLineStorage != as1::STRING::SharedEmptyText())
            ::operator delete(commandLineStorage);

        if (applicationShell->initialized())
        {
            if (applicationShell->pumpFrame() == 0)
            {
                do
                {
                } while (applicationShell->pumpFrame() == 0);
            }
        }

        as1::win::DestroyApplicationWin(applicationShell);
        as1::win::ReleaseApplicationWinHostMapCarrier();
        return 0;
    }
#endif
}

#ifndef _WIN32
int main(int argc, char** argv)
{
    return runCommandLineStartup(argc, argv, false);
}
#else
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    return runWin32Application(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
#endif
