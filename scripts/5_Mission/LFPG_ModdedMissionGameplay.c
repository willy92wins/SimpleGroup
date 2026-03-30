// ============================================================================
// LFPG_ModdedMissionGameplay.c — 5_Mission
// Client-side: inicializar cache, crear panel, keybind P para toggle
// ============================================================================

modded class MissionGameplay
{
    override void OnInit()
    {
        super.OnInit();

        // Inicializar cache del cliente
        LFPG_ClientGroupCache.Init();

        // Pre-crear el panel (oculto). GetWorkspace() es válido aquí.
        LFPG_GroupPanel.CreateInstance();

        Print("[LFPG_Territory] MissionGameplay initialized (client).");
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);

        // Check keybind P (configurable via inputs.xml)
        string inputName = "UALFPGGroupPanel";
        UAInput input = GetUApi().GetInputByName(inputName);
        if (input && input.LocalPress())
        {
            ToggleGroupPanel();
        }
    }

    protected void ToggleGroupPanel()
    {
        if (!LFPG_ClientGroupCache.HasGroup())
            return;

        LFPG_GroupPanel.Toggle();
    }

    override void OnMissionFinish()
    {
        LFPG_GroupPanel.DestroyInstance();
        LFPG_ClientGroupCache.Clear();
        super.OnMissionFinish();
    }
};
