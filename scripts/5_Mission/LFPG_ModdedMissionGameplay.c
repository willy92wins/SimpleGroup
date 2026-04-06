// ============================================================================
// LFPG_ModdedMissionGameplay.c - 5_Mission
// Client-side: inicializar cache, crear panel, keybind P para toggle
// ============================================================================

modded class MissionGameplay
{
    override void OnInit()
    {
        super.OnInit();

        // Inicializar cache del cliente
        LFPG_ClientGroupCache.Init();

        // FIX C2: Panel ya no se pre-crea. Se instancia al pulsar P.
        Print("[SimpleGroup] MissionGameplay initialized (client).");
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);

        // Check keybind U (patron VPP: GetGame().GetInput().LocalPress)
        Input input = GetGame().GetInput();
        if (input)
        {
            string inputName = "UALFPGGroupPanel";
            if (input.LocalPress(inputName, false))
            {
                UIScriptedMenu currentMenu = GetGame().GetUIManager().GetMenu();
                bool panelOpen = (LFPG_GroupPanel.GetInstance() != null);
                if (!currentMenu || panelOpen)
                {
                    ToggleGroupPanel();
                }
            }
        }
    }

    protected void ToggleGroupPanel()
    {
        // FIX C2: Toggle crea/destruye el panel (patron ScriptViewMenu)
        LFPG_GroupPanel.Toggle();
    }

    override void OnMissionFinish()
    {
        LFPG_GroupPanel.DestroyInstance();
        LFPG_ClientGroupCache.Clear();
        super.OnMissionFinish();
    }
};
