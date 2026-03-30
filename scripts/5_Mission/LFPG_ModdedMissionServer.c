// ============================================================================
// LFPG_ModdedMissionServer.c — 5_Mission
// Init del GroupManager en el server
// ============================================================================

modded class MissionServer
{
    override void OnInit()
    {
        super.OnInit();

        // Crear e inicializar el GroupManager singleton
        LFPG_GroupManager.Create();
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            mgr.Init();
        }

        Print("[LFPG_Territory] MissionServer initialized.");
    }

    override void OnMissionFinish()
    {
        // Guardar estado final antes de apagar
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            mgr.SaveGroups();
        }

        LFPG_GroupManager.Destroy();

        super.OnMissionFinish();
    }
};
