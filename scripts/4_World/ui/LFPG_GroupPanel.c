// ============================================================================
// LFPG_GroupPanel.c - 4_World/ui
// Panel de grupo (tecla P) - ScriptViewMenu con cursor y input
//
// FIX C2: Cambiado de ScriptView a ScriptViewMenu
//   - Dabs ScriptViewMenu gestiona cursor, input lock y UIManager
//   - Lifecycle: crear al abrir, destruir al cerrar (patron estandar Dabs)
//
// FIX C1: Usa LFPG_ClientGroupCache.FindLocalGroupFlag() centralizado
//   - Usa IsFlagAtPosition en vez de GetGroupID (no sincronizado en client)
//
// Dabs MVC:
//  - ViewController bindea propiedades a widgets
//  - ObservableCollection bindea member rows al WrapSpacer
//  - UseUpdateLoop = false (se actualiza solo por RPC)
// ============================================================================

class LFPG_GroupPanelController extends ViewController
{
    // Bindings (Binding_Name en layout debe coincidir EXACTAMENTE)
    string GroupName;
    string TierLabel;
    string DeployInfo;

    // ObservableCollection bindeada al WrapSpacer "MemberList"
    ref ObservableCollection<LFPG_MemberRowView> MemberRows;

    void LFPG_GroupPanelController()
    {
        GroupName = "";
        TierLabel = "";
        DeployInfo = "";
        MemberRows = new ObservableCollection<LFPG_MemberRowView>(this);
    }

    void ~LFPG_GroupPanelController()
    {
        // Romper referencias circulares (rule 21)
        if (MemberRows)
        {
            MemberRows.Clear();
        }
    }

    // Refrescar datos desde el ClientGroupCache
    void RefreshFromCache()
    {
        if (!LFPG_ClientGroupCache.HasGroup())
            return;

        GroupName = LFPG_ClientGroupCache.s_GroupName;
        string propName = "GroupName";
        NotifyPropertyChanged(propName);

        // Tier label
        string tierStr = "T";
        tierStr = tierStr + LFPG_ClientGroupCache.s_Tier.ToString();
        TierLabel = tierStr;
        string propTier = "TierLabel";
        NotifyPropertyChanged(propTier);

        // Deploy info
        string deployStr = "";
        deployStr = deployStr + LFPG_ClientGroupCache.s_DeployedCount.ToString();
        deployStr = deployStr + " / ";
        deployStr = deployStr + LFPG_ClientGroupCache.s_DeployMax.ToString();
        DeployInfo = deployStr;
        string propDeploy = "DeployInfo";
        NotifyPropertyChanged(propDeploy);

        // Refrescar member rows
        RefreshMemberRows();
    }

    protected void RefreshMemberRows()
    {
        MemberRows.Clear();

        if (!LFPG_ClientGroupCache.s_Members)
            return;

        bool localIsLeader = LFPG_ClientGroupCache.IsLeader();
        string localUID = GetLocalPlayerUID();

        int i;
        int count = LFPG_ClientGroupCache.s_Members.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_MemberData member = LFPG_ClientGroupCache.s_Members[i];
            if (!member)
                continue;

            LFPG_MemberRowView rowView = new LFPG_MemberRowView();

            bool isLeader = (member.m_PlayerUID == LFPG_ClientGroupCache.s_LeaderUID);
            bool isSelf = (member.m_PlayerUID == localUID);

            rowView.SetMemberData(member.m_PlayerUID, member.m_PlayerName, isLeader, isSelf, localIsLeader);

            MemberRows.Insert(rowView);
        }
    }

    protected string GetLocalPlayerUID()
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return "";
        PlayerIdentity identity = player.GetIdentity();
        if (!identity)
            return "";
        return identity.GetPlainId();
    }

    // Relay_Command: abandonar grupo
    // FIX C1: Usa helper centralizado del cache
    bool OnLeaveExecute(ButtonCommandArgs args)
    {
        LFPG_FlagBase flag = LFPG_ClientGroupCache.FindLocalGroupFlag();
        if (!flag)
            return false;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(flag, LFPG_RPC_C2S_REQUEST_LEAVE, true, null);

        LFPG_GroupPanel panel = LFPG_GroupPanel.GetInstance();
        if (panel)
        {
            panel.Close();
        }

        return true;
    }
};

// ============================================================================
// LFPG_GroupPanel - ScriptViewMenu (FIX C2)
// Lifecycle: crear al abrir (new), destruir al cerrar (Close/ESC)
// Dabs gestiona cursor, input lock y menu stack automaticamente
// ============================================================================
class LFPG_GroupPanel extends ScriptViewMenu
{
    protected static ref LFPG_GroupPanel s_Instance;

    void LFPG_GroupPanel()
    {
        s_Instance = this;

        // Refrescar datos del cache inmediatamente
        LFPG_GroupPanelController ctrl = LFPG_GroupPanelController.Cast(GetController());
        if (ctrl)
        {
            ctrl.RefreshFromCache();
        }

        // Solicitar sync fresco al server
        RequestGroupData();
    }

    void ~LFPG_GroupPanel()
    {
        if (s_Instance == this)
        {
            s_Instance = null;
        }
    }

    override string GetLayoutFile()
    {
        return "SimpleGroup/gui/layouts/group_panel.layout";
    }

    override typename GetControllerType()
    {
        return LFPG_GroupPanelController;
    }

    override bool UseUpdateLoop()
    {
        return false;
    }

    override bool UseMouse()
    {
        return true;
    }

    override bool UseKeyboard()
    {
        return false;
    }

    override bool CanCloseWithEscape()
    {
        return true;
    }

    override array<string> GetInputExcludes()
    {
        return {"menu"};
    }

    // ========================================================================
    // SINGLETON + TOGGLE
    // ========================================================================
    static LFPG_GroupPanel GetInstance()
    {
        return s_Instance;
    }

    static void Toggle()
    {
        if (s_Instance)
        {
            s_Instance.Close();
            return;
        }

        if (!LFPG_ClientGroupCache.HasGroup())
        {
            Print("[SimpleGroup] GroupPanel.Toggle: no group in cache, skipping.");
            return;
        }

        if (!GetGame())
            return;
        if (!GetGame().GetWorkspace())
            return;

        LFPG_GroupPanel panel = new LFPG_GroupPanel();

        // Guard: si el layout no se creo, limpiar para evitar menu fantasma
        if (!panel.GetLayoutRoot())
        {
            Print("[SimpleGroup] ERROR: GroupPanel layout failed to load. Cleaning up ghost menu.");
            s_Instance = null;
            panel = null;
            return;
        }

        // FIX AUDIT: Posicionar en script (no halign/valign en layout — modo mixto es fragil)
        // Posicion: esquina superior derecha, 10px de margen
        panel.PositionOnScreen();

        // FIX AUDIT: Forzar visibilidad explicita del layout root
        // UIManager.ShowScriptedMenu puede alterar visibilidad durante el registro
        panel.GetLayoutRoot().Show(true);

        // FIX AUDIT: Forzar carga de imagen procedural en backgrounds
        panel.InitBackgrounds();

        string dbgMsg = "[SimpleGroup] GroupPanel opened. LayoutRoot=";
        dbgMsg = dbgMsg + panel.GetLayoutRoot().GetName();
        Widget rootW = panel.GetLayoutRoot();
        float rx = 0;
        float ry = 0;
        rootW.GetScreenPos(rx, ry);
        dbgMsg = dbgMsg + " pos=";
        dbgMsg = dbgMsg + rx.ToString();
        dbgMsg = dbgMsg + ",";
        dbgMsg = dbgMsg + ry.ToString();
        Print(dbgMsg);
    }

    static void DestroyInstance()
    {
        if (s_Instance)
        {
            s_Instance.Close();
        }
    }

    // FIX AUDIT: Posicionar panel desde script
    // Evita halign/valign modo mixto (fragil segun skill UI)
    // Usa GetScreenSize para calcular esquina superior derecha
    void PositionOnScreen()
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        int screenW = 0;
        int screenH = 0;
        GetScreenSize(screenW, screenH);

        // Panel 260x320, margen 10px desde esquina superior derecha
        float posX = screenW - 270;
        float posY = 10;
        root.SetPos(posX, posY);

        string posMsg = "[SimpleGroup] Panel positioned at ";
        posMsg = posMsg + posX.ToString();
        posMsg = posMsg + ", ";
        posMsg = posMsg + posY.ToString();
        posMsg = posMsg + " (screen: ";
        posMsg = posMsg + screenW.ToString();
        posMsg = posMsg + "x";
        posMsg = posMsg + screenH.ToString();
        posMsg = posMsg + ")";
        Print(posMsg);
    }

    // FIX AUDIT: Forzar carga de imagen procedural en backgrounds
    // ImageWidget puede no renderizar solo con 'color' en layout sin image source
    void InitBackgrounds()
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        string colorTex = "#(argb,8,8,3)color(1,1,1,1,CO)";

        // PanelBg: layout color 0.12 0.12 0.14 0.92 → ARGB(235, 31, 31, 36)
        string nameBg = "PanelBg";
        ImageWidget panelBg = ImageWidget.Cast(root.FindAnyWidget(nameBg));
        if (panelBg)
        {
            panelBg.LoadImageFile(0, colorTex);
            panelBg.SetColor(ARGB(235, 31, 31, 36));
        }

        // HeaderBg: layout color 0.16 0.18 0.22 1.0 → ARGB(255, 41, 46, 56)
        string nameHeader = "HeaderBg";
        ImageWidget headerBg = ImageWidget.Cast(root.FindAnyWidget(nameHeader));
        if (headerBg)
        {
            headerBg.LoadImageFile(0, colorTex);
            headerBg.SetColor(ARGB(255, 41, 46, 56));
        }

        // DividerLine: layout color 0.3 0.3 0.35 0.6 → ARGB(153, 77, 77, 89)
        string nameDivider = "DividerLine";
        ImageWidget dividerLine = ImageWidget.Cast(root.FindAnyWidget(nameDivider));
        if (dividerLine)
        {
            dividerLine.LoadImageFile(0, colorTex);
            dividerLine.SetColor(ARGB(153, 77, 77, 89));
        }
    }

    // Llamado cuando llega nuevo sync del server
    void OnDataReceived()
    {
        LFPG_GroupPanelController ctrl = LFPG_GroupPanelController.Cast(GetController());
        if (ctrl)
        {
            ctrl.RefreshFromCache();
        }
    }

    // FIX C1: Usa helper centralizado
    protected void RequestGroupData()
    {
        LFPG_FlagBase flag = LFPG_ClientGroupCache.FindLocalGroupFlag();
        if (!flag)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(flag, LFPG_RPC_C2S_REQUEST_GROUP_DATA, true, null);
    }
};
