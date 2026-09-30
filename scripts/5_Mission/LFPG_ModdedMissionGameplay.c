// ============================================================================
// LFPG_ModdedMissionGameplay.c - 5_Mission
// Client-side: inicializar cache, crear panel, keybind P (tecla P) para toggle
// ============================================================================

modded class MissionGameplay
{
    override void OnInit()
    {
        super.OnInit();

        // Inicializar cache del cliente
        LFPG_ClientGroupCache.Init();

        // FIX I-15: Normalizar colores de UI (DayZ aplica LV negativo por default)
        Widget.SetLV(0);
        Widget.SetTextLV(0);

        // FIX C2: Panel ya no se pre-crea. Se instancia al pulsar P.
        LFPG_Log.Info("MissionGameplay initialized (client).");
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);

        // Death, unconsciousness and vanilla CloseAll leave a ScriptViewMenu
        // alive with gameplay inputs still excluded. Drop it once per instance.
        CloseGroupUiIfStale();

        Input input = GetGame().GetInput();
        if (!input)
            return;

        string inputName = "UALFPGGroupPanel";
        if (!input.LocalPress(inputName, false))
            return;

        PlayerBase localPlayer = PlayerBase.Cast(GetGame().GetPlayer());
        if (!localPlayer)
            return;
        if (!localPlayer.IsAlive())
            return;
        if (localPlayer.IsUnconscious())
            return;

        UIScriptedMenu currentMenu = GetGame().GetUIManager().GetMenu();
        bool panelOpen = (LFPG_GroupPanel.GetInstance() != null);
        if (!currentMenu || panelOpen)
        {
            ToggleGroupPanel();
        }
    }

    override void OnKeyPress(int key)
    {
        super.OnKeyPress(key);

        if (key != KeyCode.KC_ESCAPE)
            return;

        LFPG_GroupNameDialog openDialog = LFPG_GroupNameDialog.GetInstance();
        if (openDialog)
        {
            LFPG_GroupNameDialog.DestroyInstance();
            return;
        }

        LFPG_GroupPanel openPanel = LFPG_GroupPanel.GetInstance();
        if (openPanel)
        {
            LFPG_GroupPanel.DestroyInstance();
        }
    }

    // Closes the panel or the name dialog when the local player can no longer
    // use it, or when the UI manager has already dropped the menu.
    protected void CloseGroupUiIfStale()
    {
        if (!GetGame())
            return;

        PlayerBase watched = PlayerBase.Cast(GetGame().GetPlayer());
        bool playerBad = true;
        if (watched)
        {
            if (watched.IsAlive())
            {
                if (!watched.IsUnconscious())
                {
                    playerBad = false;
                }
            }
        }

        UIManager ui = GetGame().GetUIManager();

        LFPG_GroupPanel openPanel = LFPG_GroupPanel.GetInstance();
        if (openPanel)
        {
            bool panelMenuGone = true;
            UIScriptViewMenu panelMenu = openPanel.GetUIScriptViewMenu();
            if (panelMenu)
            {
                if (ui)
                {
                    int panelMenuId = panelMenu.GetID();
                    if (ui.IsMenuOpen(panelMenuId))
                    {
                        panelMenuGone = false;
                    }
                }
            }
            if (playerBad)
            {
                LFPG_GroupPanel.DestroyInstance();
            }
            else if (panelMenuGone)
            {
                LFPG_GroupPanel.DestroyInstance();
            }
        }

        LFPG_GroupNameDialog openDialog = LFPG_GroupNameDialog.GetInstance();
        if (openDialog)
        {
            bool dialogMenuGone = true;
            UIScriptViewMenu dialogMenu = openDialog.GetUIScriptViewMenu();
            if (dialogMenu)
            {
                if (ui)
                {
                    int dialogMenuId = dialogMenu.GetID();
                    if (ui.IsMenuOpen(dialogMenuId))
                    {
                        dialogMenuGone = false;
                    }
                }
            }
            if (playerBad)
            {
                LFPG_GroupNameDialog.DestroyInstance();
            }
            else if (dialogMenuGone)
            {
                LFPG_GroupNameDialog.DestroyInstance();
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
        LFPG_GroupNameDialog.DestroyInstance();
        LFPG_ClientGroupCache.Clear();
        // FIX G-10: Limpiar tracker estatico (evita leak entre sesiones de cliente)
        LFPG_DeployTracker.ClearAll();
        super.OnMissionFinish();
    }
};
