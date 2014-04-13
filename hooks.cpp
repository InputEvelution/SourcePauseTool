#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <detours.h>

#include "hooks.h"
#include "patterns.h"
#include "spt.h"

#include "convar.h"

namespace Hooks
{
    std::unordered_map<std::wstring, hookFuncList_t> moduleHookList =
    {
        {
            L"engine.dll",
            {
                EngineDll::Hook,
                EngineDll::Unhook
            }
        },

        {
            L"client.dll",
            {
                ClientDll::Hook,
                ClientDll::Unhook
            }
        }
    };

    ConVar y_spt_pause( "y_spt_pause", "1", FCVAR_ARCHIVE );
    ConVar y_spt_motion_blur_fix( "y_spt_motion_blur_fix", "0" );

    namespace Internal
    {

    }

    namespace EngineDll
    {
        struct
        {
            module_info_t moduleInfo;

            _SV_ActivateServer ORIG_SV_ActivateServer;
            _FinishRestore ORIG_FinishRestore;
            _SetPaused ORIG_SetPaused;

            DWORD_PTR pGameServer;
            bool *pM_bLoadgame;
            bool shouldPreventNextUnpause;
        } hookState;

        namespace Internal
        {
            bool __cdecl HOOKED_SV_ActivateServer( )
            {
                bool result = hookState.ORIG_SV_ActivateServer( );

                EngineDevLog( "Engine call: SV_ActivateServer() => %d;\n", result );

                if (hookState.ORIG_SetPaused && hookState.pM_bLoadgame && hookState.pGameServer)
                {
                    if ((y_spt_pause.GetInt( ) == 2) && *hookState.pM_bLoadgame)
                    {
                        hookState.ORIG_SetPaused( (void *)hookState.pGameServer, 0, true );
                        DevLog( "SPT: Pausing...\n" );

                        hookState.shouldPreventNextUnpause = true;
                    }
                }

                return result;
            }

            void __fastcall HOOKED_FinishRestore( void *thisptr, int edx )
            {
                EngineDevLog( "Engine call: FinishRestore();\n" );

                if (hookState.ORIG_SetPaused && (y_spt_pause.GetInt( ) == 1))
                {
                    hookState.ORIG_SetPaused( thisptr, 0, true );
                    DevLog( "SPT: Pausing...\n" );

                    hookState.shouldPreventNextUnpause = true;
                }

                return hookState.ORIG_FinishRestore( thisptr, edx );
            }

            void __fastcall HOOKED_SetPaused( void *thisptr, int edx, bool paused )
            {
                if (hookState.pM_bLoadgame)
                {
                    EngineDevLog( "Engine call: SetPaused(%d); m_bLoadgame = %d\n", paused, *hookState.pM_bLoadgame );
                }
                else
                {
                    EngineDevLog( "Engine call: SetPaused(%d);\n", paused );
                }

                if (paused == false)
                {
                    if (hookState.shouldPreventNextUnpause)
                    {
                        DevLog( "SPT: Unpause prevented.\n" );
                        hookState.shouldPreventNextUnpause = false;
                        return;
                    }
                }

                hookState.shouldPreventNextUnpause = false;
                return hookState.ORIG_SetPaused( thisptr, edx, paused );
            }
        }

        void Hook( std::wstring moduleName, HMODULE hModule, size_t moduleStart, size_t moduleLength )
        {
            Clear(); // Just in case.

            hookState.moduleInfo.hModule = hModule;
            hookState.moduleInfo.moduleStart = moduleStart;
            hookState.moduleInfo.moduleLength = moduleLength;

            MemUtils::ptnvec_size ptnNumber;

            // m_bLoadgame and pGameServer (&sv)
            EngineLog( "Searching for SpawnPlayer...\n" );

            DWORD_PTR pSpawnPlayer = NULL;
            ptnNumber = MemUtils::FindUniqueSequence(hookState.moduleInfo.moduleStart, hookState.moduleInfo.moduleLength, Patterns::ptnsSpawnPlayer, &pSpawnPlayer);
            if (ptnNumber != MemUtils::INVALID_SEQUENCE_INDEX)
            {
                EngineLog( "Found SpawnPlayer at %p (using the build %s pattern).\n", pSpawnPlayer, Patterns::ptnsSpawnPlayer[ptnNumber].build.c_str() );

                switch (ptnNumber)
                {
                case 0:
                    hookState.pM_bLoadgame = (bool *)(*(DWORD_PTR *)(pSpawnPlayer + 5));
                    hookState.pGameServer = (*(DWORD_PTR *)(pSpawnPlayer + 18));
                    break;

                case 1:
                    hookState.pM_bLoadgame = (bool *)(*(DWORD_PTR *)(pSpawnPlayer + 8));
                    hookState.pGameServer = (*(DWORD_PTR *)(pSpawnPlayer + 21));
                    break;

                case 2: // 4104 is the same as 5135 here.
                    hookState.pM_bLoadgame = (bool *)(*(DWORD_PTR *)(pSpawnPlayer + 5));
                    hookState.pGameServer = (*(DWORD_PTR *)(pSpawnPlayer + 18));
                    break;
                }

                EngineLog( "m_bLoadGame is situated at %p.\n", hookState.pM_bLoadgame );
                EngineLog( "pGameServer is %p.\n", hookState.pGameServer );
            }
            else
            {
                EngineWarning( "Could not find SpawnPlayer!\n" );
                EngineWarning( "y_spt_pause 2 has no effect.\n" );
            }

            // SV_ActivateServer
            EngineLog( "Searching for SV_ActivateServer...\n" );

            DWORD_PTR pSV_ActivateServer = NULL;
            ptnNumber = MemUtils::FindUniqueSequence( hookState.moduleInfo.moduleStart, hookState.moduleInfo.moduleLength, Patterns::ptnsSV_ActivateServer, &pSV_ActivateServer );
            if (ptnNumber != MemUtils::INVALID_SEQUENCE_INDEX)
            {
                hookState.ORIG_SV_ActivateServer = (_SV_ActivateServer)pSV_ActivateServer;
                EngineLog( "Found SV_ActivateServer at %p (using the build %s pattern).\n", pSV_ActivateServer, Patterns::ptnsSV_ActivateServer[ptnNumber].build.c_str() );
            }
            else
            {
                EngineWarning( "Could not find SV_ActivateServer!\n" );
                EngineWarning( "y_spt_pause 2 has no effect.\n" );
            }

            // FinishRestore
            EngineLog( "Searching for FinishRestore...\n" );

            DWORD_PTR pFinishRestore = NULL;
            ptnNumber = MemUtils::FindUniqueSequence( hookState.moduleInfo.moduleStart, hookState.moduleInfo.moduleLength, Patterns::ptnsFinishRestore, &pFinishRestore );
            if (ptnNumber != MemUtils::INVALID_SEQUENCE_INDEX)
            {
                hookState.ORIG_FinishRestore = (_FinishRestore)pFinishRestore;
                EngineLog( "Found FinishRestore at %p (using the build %s pattern).\n", pFinishRestore, Patterns::ptnsFinishRestore[ptnNumber].build.c_str() );
            }
            else
            {
                EngineWarning( "Could not find FinishRestore!\n" );
                EngineWarning( "y_spt_pause 1 has no effect.\n" );
            }

            // SetPaused
            EngineLog( "Searching for SetPaused...\n" );

            DWORD_PTR pSetPaused = NULL;
            ptnNumber = MemUtils::FindUniqueSequence( hookState.moduleInfo.moduleStart, hookState.moduleInfo.moduleLength, Patterns::ptnsSetPaused, &pSetPaused );
            if (pSetPaused)
            {
                hookState.ORIG_SetPaused = (_SetPaused)pSetPaused;
                EngineLog( "Found SetPaused at %p (using the build %s pattern).\n", pSetPaused, Patterns::ptnsSetPaused[ptnNumber].build.c_str() );
            }
            else
            {
                EngineWarning( "Could not find SetPaused!\n" );
                EngineWarning( "y_spt_pause has no effect.\n" );
            }

            if (hookState.ORIG_SV_ActivateServer
                || hookState.ORIG_FinishRestore
                || hookState.ORIG_SetPaused)
            {
                DetourTransactionBegin( );
                DetourUpdateThread( GetCurrentThread( ) );

                if (hookState.ORIG_SV_ActivateServer)
                    DetourAttach( &(PVOID &)hookState.ORIG_SV_ActivateServer, Internal::HOOKED_SV_ActivateServer );

                if (hookState.ORIG_FinishRestore)
                    DetourAttach( &(PVOID &)hookState.ORIG_FinishRestore, Internal::HOOKED_FinishRestore );

                if (hookState.ORIG_SetPaused)
                    DetourAttach( &(PVOID &)hookState.ORIG_SetPaused, Internal::HOOKED_SetPaused );

                LONG error = DetourTransactionCommit( );
                if (error == NO_ERROR)
                {
                    EngineLog( "Detoured the %s functions.\n", WStringToString( moduleName ).c_str() );
                }
                else
                {
                    EngineWarning( "Error detouring the %s functions: %d.\n", WStringToString( moduleName ).c_str(), error );
                }
            }
        }

        void Unhook( std::wstring moduleName )
        {
            if (hookState.ORIG_SV_ActivateServer
                || hookState.ORIG_FinishRestore
                || hookState.ORIG_SetPaused)
            {
                DetourTransactionBegin( );
                DetourUpdateThread( GetCurrentThread( ) );

                if (hookState.ORIG_SV_ActivateServer)
                    DetourDetach( &(PVOID &)hookState.ORIG_SV_ActivateServer, Internal::HOOKED_SV_ActivateServer );

                if (hookState.ORIG_FinishRestore)
                    DetourDetach( &(PVOID &)hookState.ORIG_FinishRestore, Internal::HOOKED_FinishRestore );

                if (hookState.ORIG_SetPaused)
                    DetourDetach( &(PVOID &)hookState.ORIG_SetPaused, Internal::HOOKED_SetPaused );

                LONG error = DetourTransactionCommit( );
                if (error == NO_ERROR)
                {
                    EngineLog( "Removed the %s function detours.\n", WStringToString( moduleName ).c_str() );
                }
                else
                {
                    EngineWarning( "Error removing the %s function detours: %d.\n", WStringToString( moduleName ).c_str(), error );
                }
            }

            Clear();
        }

        void Clear()
        {
            hookState.moduleInfo.hModule = NULL;
            hookState.moduleInfo.moduleStart = NULL;
            hookState.moduleInfo.moduleLength = NULL;
            hookState.ORIG_SV_ActivateServer = NULL;
            hookState.ORIG_FinishRestore = NULL;
            hookState.ORIG_SetPaused = NULL;
            hookState.pGameServer = NULL;
            hookState.pM_bLoadgame = NULL;
            hookState.shouldPreventNextUnpause = false;
        }
    }

    namespace ClientDll
    {
        struct
        {
            module_info_t moduleInfo;

            _DoImageSpaceMotionBlur ORIG_DoImageSpaceMorionBlur;

            DWORD_PTR *pgpGlobals;
        } hookState;

        void Hook( std::wstring moduleName, HMODULE hModule, size_t moduleStart, size_t moduleLength )
        {
            // TODO
        }

        void Unhook( std::wstring moduleName )
        {
            // TODO
        }
    }

    void Init()
    {
        // Try hooking each module in case it is already loaded
        for (auto it : moduleHookList)
        {
            HookModule( it.first );
        }
    }

    void Free()
    {
        // Unhook everything
        for (auto it : moduleHookList)
        {
            UnhookModule( it.first );
        }
    }

    void HookModule( std::wstring moduleName )
    {
        auto module = moduleHookList.find( moduleName );
        if (module != moduleHookList.end())
        {
            HMODULE targetModule;
            size_t targetModuleStart, targetModuleLength;
            if (MemUtils::GetModuleInfo( moduleName.c_str(), targetModule, targetModuleStart, targetModuleLength ))
            {
                EngineLog( "Hooking %s (start: %p; size: %x)...\n", WStringToString( moduleName ).c_str(), targetModuleStart, targetModuleLength );
                module->second.Hook( moduleName, targetModule, targetModuleStart, targetModuleLength );
            }
            else
            {
                EngineWarning( "Unable to obtain the %s module info!\n", WStringToString( moduleName ).c_str() );
            }
        }
        else
        {
            EngineDevLog( "Tried to hook an unlisted module: %s\n", WStringToString( moduleName ).c_str() );
        }
    }

    void UnhookModule( std::wstring moduleName )
    {
        auto module = moduleHookList.find( moduleName );
        if (module != moduleHookList.end())
        {
            EngineLog( "Unhooking %s...\n", WStringToString( moduleName ).c_str( ) );
            module->second.Unhook( moduleName );
        }
        else
        {
            EngineDevLog( "Tried to unhook an unlisted module: %s\n", WStringToString( moduleName ).c_str( ) );
        }
    }
}