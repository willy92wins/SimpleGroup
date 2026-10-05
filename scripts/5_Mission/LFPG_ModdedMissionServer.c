// ============================================================================
// LFPG_ModdedMissionServer.c - 5_Mission
// Init del GroupManager en el server
// ============================================================================

modded class MissionServer
{
    override void OnInit()
    {
        // FIX I-12: Crear singleton ANTES de super.OnInit para que cualquier
        // BBB/Item callback disparado durante la carga inicial tenga acceso al manager.
        LFPG_GroupManager.Create();

        super.OnInit();

        // Init despues de super (usa JsonFileLoader / config loading que necesitan estar listos)
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            mgr.Init();
        }

        LFPG_Log.Info("MissionServer initialized.");
    }

    override void OnMissionFinish()
    {
        // Guardar estado final antes de apagar
        LFPG_GroupManager mgr = LFPG_GroupManager.Get();
        if (mgr)
        {
            // El orden importa: marcar el apagado ANTES del save. Si el engine
            // borra entidades durante el teardown, cada EEDelete intentaria
            // disolver su grupo y el save final escribiria un fichero vacio.
            mgr.SetShuttingDown();
            mgr.SaveGroupsIfDirty();
            // Vehicle keys were captured from live vehicles during the session;
            // nothing is resolved from entities here.
            mgr.SaveVehicleQueuesIfDirty();
        }

        LFPG_GroupManager.Destroy();

        super.OnMissionFinish();
    }
};
