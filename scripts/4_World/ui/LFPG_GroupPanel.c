// ============================================================================
// LFPG_GroupPanel.c - 4_World/ui
// Panel de grupo (tecla U) - ScriptViewMenu con cursor y input
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
    string DeployCount;
    string GardenCount;

    // ObservableCollection bindeada al WrapSpacer "MemberList"
    ref ObservableCollection<LFPG_MemberRowView> MemberRows;

    void LFPG_GroupPanelController()
    {
        GroupName = "";
        TierLabel = "";
        DeployInfo = "";
        DeployCount = "";
        GardenCount = "";
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

        // Member count info
        string memberStr = "";
        memberStr = memberStr + LFPG_ClientGroupCache.s_MemberCount.ToString();
        memberStr = memberStr + " / ";
        memberStr = memberStr + LFPG_ClientGroupCache.s_MaxGroupSize.ToString();
        DeployInfo = memberStr;
        string propDeploy = "DeployInfo";
        NotifyPropertyChanged(propDeploy);

        // Deploy count (solo numeros, la etiqueta viene del layout con #STR_)
        string deployStr = "";
        deployStr = deployStr + LFPG_ClientGroupCache.s_DeployedCount.ToString();
        deployStr = deployStr + "/";
        deployStr = deployStr + LFPG_ClientGroupCache.s_DeployMax.ToString();
        DeployCount = deployStr;
        string propDeployCount = "DeployCount";
        NotifyPropertyChanged(propDeployCount);

        // Garden count (solo numeros, la etiqueta viene del layout con #STR_)
        string gardenStr = "";
        gardenStr = gardenStr + LFPG_ClientGroupCache.s_GardenPlotCount.ToString();
        gardenStr = gardenStr + "/";
        gardenStr = gardenStr + LFPG_ClientGroupCache.s_GardenPlotMax.ToString();
        GardenCount = gardenStr;
        string propGardenCount = "GardenCount";
        NotifyPropertyChanged(propGardenCount);

        // Refrescar member rows
        RefreshMemberRows();
    }

    // FIX I-20: Update in-place cuando el count no cambia (evita recrear widgets
    // cada sync). Solo Clear+Create si el numero de miembros difiere del current.
    protected void RefreshMemberRows()
    {
        if (!LFPG_ClientGroupCache.s_Members)
        {
            MemberRows.Clear();
            return;
        }

        bool localIsLeader = LFPG_ClientGroupCache.IsLeader();
        string localUID = LFPG_ClientGroupCache.GetLocalUID();

        int memberCount = LFPG_ClientGroupCache.s_Members.Count();
        int currentRowCount = MemberRows.Count();

        // Si el count cambio, rebuild from scratch
        if (memberCount != currentRowCount)
        {
            MemberRows.Clear();

            int i;
            for (i = 0; i < memberCount; i = i + 1)
            {
                LFPG_MemberData member = LFPG_ClientGroupCache.s_Members[i];
                if (!member)
                    continue;

                LFPG_MemberRowView rowView = new LFPG_MemberRowView();

                bool isLeader = (member.m_PlayerUID == LFPG_ClientGroupCache.s_LeaderUID);
                bool isSelf = (member.m_PlayerUID == localUID);

                rowView.SetMemberData(member.m_PlayerUID, member.m_PlayerName, isLeader, isSelf, localIsLeader, member.m_IsOnline);

                MemberRows.Insert(rowView);
            }
            return;
        }

        // Count igual: update in-place
        int j;
        for (j = 0; j < memberCount; j = j + 1)
        {
            LFPG_MemberData mdU = LFPG_ClientGroupCache.s_Members[j];
            if (!mdU)
                continue;

            LFPG_MemberRowView rowU = MemberRows.Get(j);
            if (!rowU)
                continue;

            bool isLeaderU = (mdU.m_PlayerUID == LFPG_ClientGroupCache.s_LeaderUID);
            bool isSelfU = (mdU.m_PlayerUID == localUID);
            rowU.UpdateMemberData(mdU.m_PlayerUID, mdU.m_PlayerName, isLeaderU, isSelfU, localIsLeader, mdU.m_IsOnline);
        }
    }

    // Relay_Command: abandonar grupo
    // Envia via PlayerBase (funciona desde cualquier distancia, no requiere estar cerca de flag)
    bool OnLeaveExecute(ButtonCommandArgs args)
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return false;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, LFPG_RPC_C2S_REQUEST_LEAVE, true, null);

        // FIX AUDIT: Limpiar cache optimisticamente para evitar estado stale
        // Si el server rechaza el leave, el proximo sync restaurara los datos
        LFPG_ClientGroupCache.Clear();

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
        // FIX I-17, I-18: bloquear acciones de combate/inventory mientras el panel esta abierto
        return {"menu", "inventory", "firearm", "melee"};
    }

    // ========================================================================
    // TOOLTIP HOVER (item names)
    // Overrides contiguos con los de arriba — regla Enforce #24
    // ========================================================================
    override bool OnMouseEnter(Widget w, int x, int y)
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return false;

        string deployZoneName = "DeployHoverZone";
        string gardenZoneName = "GardenHoverZone";
        Widget deployZone = root.FindAnyWidget(deployZoneName);
        Widget gardenZone = root.FindAnyWidget(gardenZoneName);

        bool isDeploy = (w == deployZone);
        bool isGarden = (w == gardenZone);

        if (!isDeploy && !isGarden)
            return false;

        string tooltipName = "ItemTooltip";
        Widget tooltip = root.FindAnyWidget(tooltipName);
        if (!tooltip)
            return false;

        string textName = "TooltipText";
        TextWidget tooltipText = TextWidget.Cast(root.FindAnyWidget(textName));
        if (!tooltipText)
            return false;

        string bgName = "TooltipBg";
        Widget tooltipBg = root.FindAnyWidget(bgName);

        // Construir texto con nombres del cache
        string content = "";
        int itemCount = 0;

        if (isDeploy && LFPG_ClientGroupCache.s_DeployedItemNames)
        {
            itemCount = LFPG_ClientGroupCache.s_DeployedItemNames.Count();
            int d;
            for (d = 0; d < itemCount; d = d + 1)
            {
                if (d > 0)
                {
                    content = content + "\n";
                }
                content = content + LFPG_ClientGroupCache.s_DeployedItemNames[d];
            }
        }
        else if (isGarden && LFPG_ClientGroupCache.s_GardenItemNames)
        {
            itemCount = LFPG_ClientGroupCache.s_GardenItemNames.Count();
            int g;
            for (g = 0; g < itemCount; g = g + 1)
            {
                if (g > 0)
                {
                    content = content + "\n";
                }
                content = content + LFPG_ClientGroupCache.s_GardenItemNames[g];
            }
        }

        if (itemCount == 0)
        {
            string emptyStr = "#STR_LFPG_UI_TOOLTIP_EMPTY";
            content = emptyStr;
            itemCount = 1;
        }

        tooltipText.SetText(content);

        // Ajustar altura del tooltip (14px por linea + 8px padding)
        float lineHeight = 14.0;
        float padding = 8.0;
        float tooltipHeight = (itemCount * lineHeight) + padding;
        tooltip.SetSize(244.0, tooltipHeight);
        if (tooltipBg)
        {
            tooltipBg.SetSize(244.0, tooltipHeight);
        }
        float textHeight = tooltipHeight - padding;
        tooltipText.SetSize(232.0, textHeight);

        tooltip.Show(true);
        return true;
    }

    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return false;

        // No ocultar si el mouse se mueve a la otra hover zone (evita parpadeo)
        string deployZoneLeave = "DeployHoverZone";
        string gardenZoneLeave = "GardenHoverZone";
        Widget deployZoneW = root.FindAnyWidget(deployZoneLeave);
        Widget gardenZoneW = root.FindAnyWidget(gardenZoneLeave);
        if (enterW == deployZoneW || enterW == gardenZoneW)
            return false;

        string tooltipNameLeave = "ItemTooltip";
        Widget tooltip = root.FindAnyWidget(tooltipNameLeave);
        if (tooltip)
        {
            tooltip.Show(false);
        }
        return false;
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
            // FIX I-16: Feedback visual en vez de silent no-op
            float duration = 4.0;
            string title = "#STR_LFPG_MOD_NAME";
            string body = "#STR_LFPG_BUILD_NO_TERRITORY";
            string icon = "set:dayz_gui image:ui_info";
            NotificationSystem.AddNotificationExtended(duration, title, body, icon);
            LFPG_Log.Debug("GroupPanel.Toggle: no group in cache.");
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
            LFPG_Log.Error("GroupPanel layout failed to load. Cleaning up ghost menu.");
            s_Instance = null;
            panel = null;
            return;
        }

        // Posicionar con Widget.GetScreenSize (escalado correcto a cualquier res)
        panel.PositionOnScreen();

        // FIX AUDIT: Forzar visibilidad explicita del layout root
        // UIManager.ShowScriptedMenu puede alterar visibilidad durante el registro
        panel.GetLayoutRoot().Show(true);

        // FIX AUDIT: Forzar carga de imagen procedural en backgrounds
        panel.InitBackgrounds();
    }

    static void DestroyInstance()
    {
        if (s_Instance)
        {
            s_Instance.Close();
        }
    }

    // Posicionar panel en esquina superior derecha
    // Patron Dabs: usa Widget.GetScreenSize para obtener tamanos RENDERIZADOS
    // que incluyen UI scaling (260 layout px = 520 rendered px a 4K 2x scale)
    // Funciona a cualquier resolucion y nivel de UI scale
    void PositionOnScreen()
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        Widget parent = root.GetParent();
        if (!parent)
            return;

        // Tamano del parent (pantalla) en pixels de pantalla
        float pW = 0;
        float pH = 0;
        parent.GetScreenSize(pW, pH);

        // Tamano RENDERIZADO del panel (incluye UI scaling)
        float wW = 0;
        float wH = 0;
        root.GetScreenSize(wW, wH);

        // Margen proporcional al tamano renderizado del widget (aprox 4% del ancho)
        float margin = 10.0;
        if (wW > 0)
        {
            margin = wW * 0.038;
        }

        float posX = pW - wW - margin;
        float posY = margin;
        root.SetPos(posX, posY);
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

        // HeaderBg: layout color 0.16 0.18 0.22 1.0 → ARGB(255, 41, 46, 56) (56px height)
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

        // TooltipBg: layout color 0.08 0.08 0.10 0.95 → ARGB(242, 20, 20, 26)
        string nameTooltipBg = "TooltipBg";
        ImageWidget tooltipBg = ImageWidget.Cast(root.FindAnyWidget(nameTooltipBg));
        if (tooltipBg)
        {
            tooltipBg.LoadImageFile(0, colorTex);
            tooltipBg.SetColor(ARGB(242, 20, 20, 26));
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

    // Solicitar datos completos al server via PlayerBase
    protected void RequestGroupData()
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, LFPG_RPC_C2S_REQUEST_GROUP_DATA, true, null);
    }
};
