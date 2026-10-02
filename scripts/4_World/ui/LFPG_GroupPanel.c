// ============================================================================
// LFPG_GroupPanel.c - 4_World/ui
// Group panel. ScriptViewMenu with cursor and input. The open key is rebindable.
//
// The layout anchors the panel and paints the window. Dabs MVC binds GroupName,
// TerritoryStatus and MemberRows. TerritoryStatus is the tier line only.
// ============================================================================

class LFPG_GroupPanelController extends ViewController
{
    // Bindings (Binding_Name en layout debe coincidir EXACTAMENTE)
    string GroupName;
    string TerritoryStatus;

    static const int BTN_IDLE = ARGB(0, 0, 0, 0);
    static const int BTN_HOT = ARGB(255, 255, 0, 0);
    static const int ICON = ARGB(255, 160, 160, 160);
    static const int ICON_HOT = ARGB(255, 255, 255, 255);

    // Second click of Leave must arrive before this mission time (ms).
    protected int m_LeaveConfirmUntil;
    // True when the armed Leave caption is the dissolve warning.
    protected bool m_LeaveArmedDissolve;
    // Leave fill is hot while this is set or the pointer is over the button.
    protected bool m_LeaveArmed;
    protected bool m_LeaveHover;
    // Last tier painted on TerritoryText. The open panel compares it each tick.
    protected int m_SeenTier;

    // ObservableCollection bound to MemberList.
    ref ObservableCollection<LFPG_MemberRowView> MemberRows;

    void LFPG_GroupPanelController()
    {
        GroupName = "";
        TerritoryStatus = "";
        m_LeaveConfirmUntil = 0;
        m_LeaveArmedDissolve = false;
        m_LeaveArmed = false;
        m_LeaveHover = false;
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

    // Tier only. The radius and the flag progress are not shown here.
    protected void ApplyTerritoryStatus()
    {
        string tierStr = LFPG_ClientGroupCache.s_Tier.ToString();
        string territoryFmt = Widget.TranslateString("#STR_LFPG_UI_TERRITORY_FMT");
        TerritoryStatus = string.Format(territoryFmt, tierStr);

        string propTerritory = "TerritoryStatus";
        NotifyPropertyChanged(propTerritory);
        m_SeenTier = LFPG_ClientGroupCache.s_Tier;
    }

    // Expired Leave confirm and a tier change. No allocations.
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

        if (m_SeenTier != LFPG_ClientGroupCache.s_Tier)
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
        if (m_LayoutRoot)
        {
            string leaveTextName = "BtnLeaveText";
            TextWidget leaveText = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(leaveTextName));
            if (leaveText)
            {
                string shown = Widget.TranslateString(stringId);
                leaveText.SetText(shown);
            }
        }

        m_LeaveArmed = armed;
        PaintLeaveButton();
    }

    // Hot while the confirm is armed or the pointer is over the button.
    protected void PaintLeaveButton()
    {
        if (!m_LayoutRoot)
            return;

        string leavePanelName = "BtnLeavePanel";
        Widget leavePanel = m_LayoutRoot.FindAnyWidget(leavePanelName);
        if (!leavePanel)
            return;

        bool leaveHot = false;
        if (m_LeaveArmed)
        {
            leaveHot = true;
        }
        if (m_LeaveHover)
        {
            leaveHot = true;
        }
        if (leaveHot)
        {
            leavePanel.SetColor(BTN_HOT);
        }
        else
        {
            leavePanel.SetColor(BTN_IDLE);
        }
    }

    // Close has no confirm. The pointer alone chooses the fill and the icon.
    protected void PaintCloseButton(bool hot)
    {
        if (!m_LayoutRoot)
            return;

        string closePanelName = "BtnClosePanel";
        Widget closePanel = m_LayoutRoot.FindAnyWidget(closePanelName);
        if (closePanel)
        {
            if (hot)
            {
                closePanel.SetColor(BTN_HOT);
            }
            else
            {
                closePanel.SetColor(BTN_IDLE);
            }
        }

        string closeIconName = "BtnCloseIcon";
        Widget closeIcon = m_LayoutRoot.FindAnyWidget(closeIconName);
        if (closeIcon)
        {
            if (hot)
            {
                closeIcon.SetColor(ICON_HOT);
            }
            else
            {
                closeIcon.SetColor(ICON);
            }
        }
    }

    override bool OnMouseEnter(Widget w, int x, int y)
    {
        if (!w)
            return false;

        string enteredName = w.GetName();
        if (enteredName == "BtnLeave")
        {
            m_LeaveHover = true;
            PaintLeaveButton();
        }
        else if (enteredName == "BtnClose")
        {
            PaintCloseButton(true);
        }
        return false;
    }

    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        if (!w)
            return false;

        string leftName = w.GetName();
        if (leftName == "BtnLeave")
        {
            m_LeaveHover = false;
            PaintLeaveButton();
        }
        else if (leftName == "BtnClose")
        {
            PaintCloseButton(false);
        }
        return false;
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
        // Leave confirm expiry and a tier change. No per-frame allocation.
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
