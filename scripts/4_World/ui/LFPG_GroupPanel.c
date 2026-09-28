// ============================================================================
// LFPG_GroupPanel.c - 4_World/ui
// Group panel. ScriptViewMenu with cursor and input. The open key is rebindable.
//
// The layout anchors the panel on the screen and paints its own backgrounds.
// Dabs MVC binds GroupName, TerritoryStatus, FlagLabel, FlagPercent,
// MembersValue, FurnitureValue and GardenValue. MemberRows fills the member list.
// ============================================================================

class LFPG_GroupPanelController extends ViewController
{
    // Bindings (Binding_Name en layout debe coincidir EXACTAMENTE)
    string GroupName;
    string TerritoryStatus;
    string FlagLabel;
    string FlagPercent;
    string MembersValue;
    string FurnitureValue;
    string GardenValue;

    static const int COLOR_TEXT = ARGB(255, 236, 236, 236);
    static const int COLOR_TEXT2 = ARGB(255, 154, 154, 154);
    static const int COLOR_AMBER = ARGB(255, 214, 160, 48);
    static const int COLOR_RED = ARGB(255, 210, 70, 70);
    static const int COLOR_GREEN = ARGB(255, 106, 159, 74);
    static const int COLOR_NEUTRAL_FILL = ARGB(255, 140, 140, 140);
    static const int COLOR_LEAVE = ARGB(255, 90, 31, 27);
    static const int COLOR_LEAVE_ARMED = ARGB(255, 138, 42, 36);

    // Second click of Leave must arrive before this mission time (ms).
    protected int m_LeaveConfirmUntil;
    // True when the armed Leave caption is the dissolve warning.
    protected bool m_LeaveArmedDissolve;
    // Last cache values painted on TerritoryText. The flag sync writes the
    // cache without an RPC, so the open panel compares these each tick.
    protected float m_SeenFlagProgress;
    protected int m_SeenTier;

    // ObservableCollection bindeada al WrapSpacer "MemberList"
    ref ObservableCollection<LFPG_MemberRowView> MemberRows;

    void LFPG_GroupPanelController()
    {
        GroupName = "";
        TerritoryStatus = "";
        FlagLabel = "";
        FlagPercent = "";
        MembersValue = "";
        FurnitureValue = "";
        GardenValue = "";
        m_LeaveConfirmUntil = 0;
        m_LeaveArmedDissolve = false;
        m_SeenFlagProgress = -1.0;
        m_SeenTier = -1;
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

        string rawName = LFPG_ClientGroupCache.s_GroupName;
        if (rawName.IndexOf("#TEMP#") == 0)
        {
            GroupName = Widget.TranslateString("#STR_LFPG_UI_UNNAMED");
        }
        else
        {
            GroupName = rawName;
        }
        string propName = "GroupName";
        NotifyPropertyChanged(propName);

        ApplyTerritoryStatus();

        string memberStr = LFPG_ClientGroupCache.s_MemberCount.ToString();
        memberStr = memberStr + "/";
        memberStr = memberStr + LFPG_ClientGroupCache.s_MaxGroupSize.ToString();
        MembersValue = memberStr;
        string propMembers = "MembersValue";
        NotifyPropertyChanged(propMembers);
        ApplyLimit("MembersValueText", "MembersBarFill", LFPG_ClientGroupCache.s_MemberCount, LFPG_ClientGroupCache.s_MaxGroupSize, false);

        string deployStr = LFPG_ClientGroupCache.s_DeployedCount.ToString();
        deployStr = deployStr + "/";
        deployStr = deployStr + LFPG_ClientGroupCache.s_DeployMax.ToString();
        FurnitureValue = deployStr;
        string propFurniture = "FurnitureValue";
        NotifyPropertyChanged(propFurniture);
        ApplyLimit("FurnitureValueText", "FurnitureBarFill", LFPG_ClientGroupCache.s_DeployedCount, LFPG_ClientGroupCache.s_DeployMax, true);

        string gardenStr = LFPG_ClientGroupCache.s_GardenPlotCount.ToString();
        gardenStr = gardenStr + "/";
        gardenStr = gardenStr + LFPG_ClientGroupCache.s_GardenPlotMax.ToString();
        GardenValue = gardenStr;
        string propGarden = "GardenValue";
        NotifyPropertyChanged(propGarden);
        ApplyLimit("GardenValueText", "GardenBarFill", LFPG_ClientGroupCache.s_GardenPlotCount, LFPG_ClientGroupCache.s_GardenPlotMax, true);

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

    // Tier and radius stay visible. Flag label and bar follow raise progress.
    protected void ApplyTerritoryStatus()
    {
        float radiusFloat = Math.Sqrt(LFPG_ClientGroupCache.s_BuildRadiusSq);
        int radiusM = radiusFloat;
        string tierStr = LFPG_ClientGroupCache.s_Tier.ToString();
        string radiusStr = radiusM.ToString();
        string territoryFmt = Widget.TranslateString("#STR_LFPG_UI_TERRITORY_FMT");
        TerritoryStatus = string.Format(territoryFmt, tierStr, radiusStr);

        bool flagDown = false;
        if (LFPG_ClientGroupCache.s_FlagRaiseProgress <= 0.0)
        {
            flagDown = true;
        }

        float progress = LFPG_ClientGroupCache.s_FlagRaiseProgress;
        if (progress < 0.0)
        {
            progress = 0.0;
        }
        if (progress > 1.0)
        {
            progress = 1.0;
        }

        if (flagDown)
        {
            FlagLabel = Widget.TranslateString("#STR_LFPG_UI_FLAG_DOWN");
            FlagPercent = "";
        }
        else
        {
            FlagLabel = Widget.TranslateString("#STR_LFPG_UI_FLAG_RAISED");
            float pctFloat = progress * 100.0;
            int pct = pctFloat;
            FlagPercent = pct.ToString();
            FlagPercent = FlagPercent + "%";
        }

        string propTerritory = "TerritoryStatus";
        NotifyPropertyChanged(propTerritory);
        string propFlagLabel = "FlagLabel";
        NotifyPropertyChanged(propFlagLabel);
        string propFlagPercent = "FlagPercent";
        NotifyPropertyChanged(propFlagPercent);
        m_SeenFlagProgress = LFPG_ClientGroupCache.s_FlagRaiseProgress;
        m_SeenTier = LFPG_ClientGroupCache.s_Tier;

        if (!m_LayoutRoot)
            return;

        string flagLabelName = "FlagLabelText";
        TextWidget flagLabelWidget = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(flagLabelName));
        string flagFillName = "FlagBarFill";
        Widget flagFill = m_LayoutRoot.FindAnyWidget(flagFillName);

        if (flagDown)
        {
            if (flagLabelWidget)
            {
                flagLabelWidget.SetColor(COLOR_RED);
            }
            if (flagFill)
            {
                flagFill.Show(false);
            }
        }
        else
        {
            if (flagLabelWidget)
            {
                flagLabelWidget.SetColor(COLOR_TEXT2);
            }
            int barColor = COLOR_RED;
            if (progress >= 0.25)
            {
                barColor = COLOR_GREEN;
            }
            else if (progress >= 0.10)
            {
                barColor = COLOR_AMBER;
            }
            if (flagFill)
            {
                float barW = 308.0 * progress;
                flagFill.SetSize(barW, 6.0);
                flagFill.SetColor(barColor);
                flagFill.Show(true);
            }
        }
    }

    // Members never warn. Furniture and gardens warn from 75 percent and at the cap.
    protected void ApplyLimit(string valueWidget, string fillWidget, int count, int max, bool warn)
    {
        float fillW = 97.0;
        if (max > 0)
        {
            fillW = 97.0 * count;
            fillW = fillW / max;
        }
        if (fillW < 0.0)
        {
            fillW = 0.0;
        }
        if (fillW > 97.0)
        {
            fillW = 97.0;
        }

        int valueColor = COLOR_TEXT;
        int fillColor = COLOR_NEUTRAL_FILL;
        if (warn)
        {
            bool atCap = false;
            if (max <= 0)
            {
                atCap = true;
            }
            if (count >= max)
            {
                atCap = true;
            }
            if (atCap)
            {
                valueColor = COLOR_RED;
                fillColor = COLOR_RED;
            }
            else
            {
                int usedScaled = count * 100;
                int amberAt = max * 75;
                if (usedScaled >= amberAt)
                {
                    valueColor = COLOR_AMBER;
                    fillColor = COLOR_AMBER;
                }
            }
        }

        if (!m_LayoutRoot)
            return;

        TextWidget valueText = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(valueWidget));
        if (valueText)
        {
            valueText.SetColor(valueColor);
        }
        Widget fill = m_LayoutRoot.FindAnyWidget(fillWidget);
        if (fill)
        {
            fill.SetSize(fillW, 3.0);
            fill.SetColor(fillColor);
        }
    }

    // Flag sync and an expired Leave confirm. No allocations.
    void TickOpenPanel(int nowMs)
    {
        if (m_LeaveConfirmUntil > 0)
        {
            if (nowMs > m_LeaveConfirmUntil)
            {
                m_LeaveConfirmUntil = 0;
                m_LeaveArmedDissolve = false;
                SetLeaveCaption("#STR_LFPG_UI_LEAVE_GROUP", false);
            }
        }

        bool territoryStale = false;
        if (m_SeenFlagProgress != LFPG_ClientGroupCache.s_FlagRaiseProgress)
        {
            territoryStale = true;
        }
        if (m_SeenTier != LFPG_ClientGroupCache.s_Tier)
        {
            territoryStale = true;
        }
        if (territoryStale)
        {
            ApplyTerritoryStatus();
        }

        if (!MemberRows)
            return;

        int rowCount = MemberRows.Count();
        int rowIndex;
        for (rowIndex = 0; rowIndex < rowCount; rowIndex = rowIndex + 1)
        {
            LFPG_MemberRowView rowView = MemberRows.Get(rowIndex);
            if (rowView)
            {
                rowView.ExpireConfirm(nowMs);
            }
        }
    }

    protected void SetLeaveCaption(string stringId, bool armed)
    {
        if (!m_LayoutRoot)
            return;

        string leaveTextName = "BtnLeaveText";
        TextWidget leaveText = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(leaveTextName));
        if (leaveText)
        {
            string shown = Widget.TranslateString(stringId);
            leaveText.SetText(shown);
        }

        string leaveBtnName = "BtnLeave";
        ButtonWidget leaveBtn = ButtonWidget.Cast(m_LayoutRoot.FindAnyWidget(leaveBtnName));
        if (leaveBtn)
        {
            if (armed)
            {
                leaveBtn.SetColor(COLOR_LEAVE_ARMED);
            }
            else
            {
                leaveBtn.SetColor(COLOR_LEAVE);
            }
        }
    }

    // Relay_Command: header close. One request per instance.
    bool OnCloseExecute(ButtonCommandArgs args)
    {
        LFPG_GroupPanel.DestroyInstance();
        return true;
    }

    // Relay_Command: abandonar grupo. Second click within 3s sends the RPC.
    // The cache stays until the server answers; GROUP_DISSOLVED closes the panel.
    bool OnLeaveExecute(ButtonCommandArgs args)
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return false;

        if (!GetGame())
            return false;

        int nowLeave = GetGame().GetTime();
        bool leaveArmed = false;
        if (m_LeaveConfirmUntil > 0)
        {
            if (nowLeave <= m_LeaveConfirmUntil)
            {
                leaveArmed = true;
            }
        }
        bool lastMemberNow = false;
        if (LFPG_ClientGroupCache.s_MemberCount <= 1)
        {
            lastMemberNow = true;
        }

        if (!leaveArmed)
        {
            m_LeaveConfirmUntil = nowLeave + 3000;
            m_LeaveArmedDissolve = lastMemberNow;
            string confirmId = "#STR_LFPG_UI_CONFIRM_LEAVE";
            if (lastMemberNow)
            {
                confirmId = "#STR_LFPG_UI_CONFIRM_DISSOLVE";
            }
            SetLeaveCaption(confirmId, true);
            return true;
        }

        // Armed as a normal leave, but this player is now the last member.
        if (lastMemberNow)
        {
            if (!m_LeaveArmedDissolve)
            {
                m_LeaveConfirmUntil = nowLeave + 3000;
                m_LeaveArmedDissolve = true;
                SetLeaveCaption("#STR_LFPG_UI_CONFIRM_DISSOLVE", true);
                return true;
            }
        }

        m_LeaveConfirmUntil = 0;
        m_LeaveArmedDissolve = false;
        SetLeaveCaption("#STR_LFPG_UI_LEAVE_GROUP", false);

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(player, LFPG_RPC_C2S_REQUEST_LEAVE, true, null);
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

    // Close() deletes on a later GUI tick. A second Close in that window double-frees.
    protected bool m_CloseRequested;

    // Tooltip body built on hover. Not allocated per frame.
    protected string m_TooltipBody;
    protected int m_TooltipLines;

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
        // Flag raise progress is written on the client without an RPC.
        return true;
    }

    override void Update(float dt)
    {
        if (!GetGame())
            return;

        LFPG_GroupPanelController openCtrl = LFPG_GroupPanelController.Cast(GetController());
        if (!openCtrl)
            return;

        int nowMs = GetGame().GetTime();
        openCtrl.TickOpenPanel(nowMs);
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

        string deployZoneName = "FurnitureHoverZone";
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
        MultilineTextWidget tooltipText = MultilineTextWidget.Cast(root.FindAnyWidget(textName));
        if (!tooltipText)
            return false;

        string bgName = "TooltipBg";
        Widget tooltipBg = root.FindAnyWidget(bgName);

        m_TooltipBody = "";
        m_TooltipLines = 0;
        if (isDeploy)
        {
            BuildGroupedTooltip(LFPG_ClientGroupCache.s_DeployedItemNames);
        }
        else if (isGarden)
        {
            BuildGroupedTooltip(LFPG_ClientGroupCache.s_GardenItemNames);
        }

        string tooltipShown = m_TooltipBody;
        int lineCount = m_TooltipLines;
        if (lineCount == 0)
        {
            tooltipShown = Widget.TranslateString("#STR_LFPG_UI_TOOLTIP_EMPTY");
            lineCount = 1;
        }

        tooltipText.SetText(tooltipShown);

        // One row per grouped line, plus padding.
        float lineHeight = 20.0;
        float padding = 12.0;
        float tooltipHeight = (lineCount * lineHeight) + padding;
        float textHeight = lineCount * lineHeight;
        tooltip.SetSize(308.0, tooltipHeight);
        if (tooltipBg)
        {
            tooltipBg.SetSize(308.0, tooltipHeight);
        }
        tooltipText.SetSize(288.0, textHeight);

        tooltip.Show(true);
        return true;
    }

    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return false;

        // No ocultar si el mouse se mueve a la otra hover zone (evita parpadeo)
        string deployZoneLeave = "FurnitureHoverZone";
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
            s_Instance.RequestCloseOnce();
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

        // FIX AUDIT: Forzar visibilidad explicita del layout root
        // UIManager.ShowScriptedMenu puede alterar visibilidad durante el registro
        panel.GetLayoutRoot().Show(true);
    }

    static void DestroyInstance()
    {
        if (s_Instance)
        {
            s_Instance.RequestCloseOnce();
        }
    }

    // ScriptViewMenu.Close schedules delete. Ignore a second request.
    void RequestCloseOnce()
    {
        if (m_CloseRequested)
            return;

        m_CloseRequested = true;
        Close();
    }

    // Collapse duplicate item names into "Name x3", one entry per line.
    protected void BuildGroupedTooltip(array<string> names)
    {
        m_TooltipBody = "";
        m_TooltipLines = 0;
        if (!names)
            return;

        array<string> uniqueNames = new array<string>;
        array<int> uniqueCounts = new array<int>;
        int nameCount = names.Count();
        int n;
        for (n = 0; n < nameCount; n = n + 1)
        {
            string oneName = names[n];
            int found = uniqueNames.Find(oneName);
            if (found >= 0)
            {
                int prevCount = uniqueCounts[found];
                uniqueCounts[found] = prevCount + 1;
            }
            else
            {
                uniqueNames.Insert(oneName);
                uniqueCounts.Insert(1);
            }
        }

        int uniqueTotal = uniqueNames.Count();
        int u;
        for (u = 0; u < uniqueTotal; u = u + 1)
        {
            if (u > 0)
            {
                m_TooltipBody = m_TooltipBody + "\n";
            }
            string line = uniqueNames[u];
            int copies = uniqueCounts[u];
            if (copies > 1)
            {
                line = line + " x";
                line = line + copies.ToString();
            }
            m_TooltipBody = m_TooltipBody + line;
        }
        m_TooltipLines = uniqueTotal;
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
