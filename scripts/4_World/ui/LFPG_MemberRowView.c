// ============================================================================
// LFPG_MemberRowView.c - 4_World/ui
// One member row in the group panel. Dabs ScriptView plus ViewController.
//
// Layout bindings:
//  - LeaderTag: localized "Leader" on the leader row, empty otherwise
//  - MemberName: player name, with the localized "(you)" on the local row
//  - BtnTransfer: visible to the leader, hidden on the leader's own row
//  - BtnKick: visible to the leader, hidden on the leader's own row
//
// An offline name is grey. The row has no online dot.
// ============================================================================

class LFPG_MemberRowController extends ViewController
{
    // Bound properties (nombres deben coincidir con Binding_Name del layout)
    string LeaderTag;
    string MemberName;

    static const int BTN_IDLE = ARGB(0, 0, 0, 0);
    static const int BTN_HOT = ARGB(255, 255, 0, 0);

    // Datos internos (no bindeados)
    string m_MemberUID;
    bool m_IsLeader;
    bool m_IsSelf;
    bool m_IsOnline;

    // Second click must arrive before this mission time (ms). 0 means not armed.
    protected int m_KickConfirmUntil;
    protected int m_TransferConfirmUntil;
    protected bool m_TransferHover;
    protected bool m_KickHover;

    // Buttons (loaded by LoadWidgetsAsVariables)
    ButtonWidget BtnTransfer;
    ButtonWidget BtnKick;

    void LFPG_MemberRowController()
    {
        LeaderTag = "";
        MemberName = "";
        m_MemberUID = "";
        m_IsLeader = false;
        m_IsSelf = false;
        m_IsOnline = false;
        m_KickConfirmUntil = 0;
        m_TransferConfirmUntil = 0;
        m_TransferHover = false;
        m_KickHover = false;
    }

    void SetData(string uid, string name, bool isLeader, bool isLocalPlayer, bool localIsLeader, bool isOnline)
    {
        bool sameMember = false;
        if (m_MemberUID == uid)
        {
            sameMember = true;
        }

        m_MemberUID = uid;
        m_IsLeader = isLeader;
        m_IsSelf = isLocalPlayer;

        if (isLeader)
        {
            LeaderTag = Widget.TranslateString("#STR_LFPG_UI_LEADER");
        }
        else
        {
            LeaderTag = "";
        }

        MemberName = name;
        if (isLocalPlayer)
        {
            string youTag = Widget.TranslateString("#STR_LFPG_UI_YOU");
            MemberName = MemberName + " ";
            MemberName = MemberName + youTag;
        }

        string propStar = "LeaderTag";
        NotifyPropertyChanged(propStar);

        string propName = "MemberName";
        NotifyPropertyChanged(propName);

        // Botones: solo visibles si el local es lider Y esta fila no es el local
        bool showButtons = false;
        if (localIsLeader && !isLocalPlayer)
        {
            showButtons = true;
        }

        if (BtnTransfer)
        {
            BtnTransfer.Show(showButtons);
        }
        if (BtnKick)
        {
            BtnKick.Show(showButtons);
        }

        if (m_LayoutRoot)
        {
            string nameClipName = "MemberNameClip";
            Widget nameClip = m_LayoutRoot.FindAnyWidget(nameClipName);
            string nameTextName = "MemberNameText";
            TextWidget nameText = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(nameTextName));
            if (nameText)
            {
                if (showButtons)
                {
                    nameText.SetSize(164.0, 34.0);
                    if (nameClip)
                        nameClip.SetSize(164.0, 34.0);
                }
                else
                {
                    nameText.SetSize(250.0, 34.0);
                    if (nameClip)
                        nameClip.SetSize(250.0, 34.0);
                }
            }
        }

        m_IsOnline = isOnline;

        // A recycled row must not keep the previous member's confirm.
        if (!sameMember)
        {
            m_KickConfirmUntil = 0;
            m_TransferConfirmUntil = 0;
            SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
            SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
            PaintRowButtons();
        }
    }

    // Hot while that button's confirm is armed or the pointer is over it.
    protected void PaintRowButtons()
    {
        if (!m_LayoutRoot)
            return;

        string transferPanelName = "BtnTransferPanel";
        Widget transferPanel = m_LayoutRoot.FindAnyWidget(transferPanelName);
        if (transferPanel)
        {
            bool transferHot = false;
            if (m_TransferConfirmUntil > 0)
            {
                transferHot = true;
            }
            if (m_TransferHover)
            {
                transferHot = true;
            }
            if (transferHot)
            {
                transferPanel.SetColor(BTN_HOT);
            }
            else
            {
                transferPanel.SetColor(BTN_IDLE);
            }
        }

        string kickPanelName = "BtnKickPanel";
        Widget kickPanel = m_LayoutRoot.FindAnyWidget(kickPanelName);
        if (kickPanel)
        {
            bool kickHot = false;
            if (m_KickConfirmUntil > 0)
            {
                kickHot = true;
            }
            if (m_KickHover)
            {
                kickHot = true;
            }
            if (kickHot)
            {
                kickPanel.SetColor(BTN_HOT);
            }
            else
            {
                kickPanel.SetColor(BTN_IDLE);
            }
        }
    }

    override bool OnMouseEnter(Widget w, int x, int y)
    {
        if (!w)
            return false;

        string enteredName = w.GetName();
        if (enteredName == "BtnTransfer")
        {
            m_TransferHover = true;
            PaintRowButtons();
        }
        else if (enteredName == "BtnKick")
        {
            m_KickHover = true;
            PaintRowButtons();
        }
        return false;
    }

    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        if (!w)
            return false;

        string leftName = w.GetName();
        if (leftName == "BtnTransfer")
        {
            m_TransferHover = false;
            PaintRowButtons();
        }
        else if (leftName == "BtnKick")
        {
            m_KickHover = false;
            PaintRowButtons();
        }
        return false;
    }

    protected void SetCaption(string widgetName, string stringId)
    {
        if (!m_LayoutRoot)
            return;

        TextWidget caption = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(widgetName));
        if (!caption)
            return;

        string shown = Widget.TranslateString(stringId);
        caption.SetText(shown);
    }

    // Relay_Command: transferir liderazgo. Second click within 3s sends the RPC.
    bool OnTransferExecute(ButtonCommandArgs args)
    {
        if (m_MemberUID == "")
            return false;
        if (!GetGame())
            return false;

        int nowTransfer = GetGame().GetTime();
        bool transferArmed = false;
        if (m_TransferConfirmUntil > 0)
        {
            if (nowTransfer <= m_TransferConfirmUntil)
            {
                transferArmed = true;
            }
        }
        if (!transferArmed)
        {
            m_TransferConfirmUntil = nowTransfer + 3000;
            SetCaption("BtnTransferLabel", "#STR_LFPG_UI_CONFIRM_TRANSFER");
            PaintRowButtons();
            return true;
        }

        m_TransferConfirmUntil = 0;
        SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
        PaintRowButtons();
        SendMemberRPC(LFPG_RPC_C2S_REQUEST_TRANSFER, m_MemberUID);
        return true;
    }

    // Called from the open panel tick. Restores the label after the window.
    void ExpireConfirm(int nowMs)
    {
        if (m_KickConfirmUntil > 0)
        {
            if (nowMs > m_KickConfirmUntil)
            {
                m_KickConfirmUntil = 0;
                SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
                PaintRowButtons();
            }
        }
        if (m_TransferConfirmUntil > 0)
        {
            if (nowMs > m_TransferConfirmUntil)
            {
                m_TransferConfirmUntil = 0;
                SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
                PaintRowButtons();
            }
        }
    }

    // Relay_Command: expulsar. Second click within 3s sends the RPC.
    bool OnKickExecute(ButtonCommandArgs args)
    {
        if (m_MemberUID == "")
            return false;
        if (!GetGame())
            return false;

        int nowKick = GetGame().GetTime();
        bool kickArmed = false;
        if (m_KickConfirmUntil > 0)
        {
            if (nowKick <= m_KickConfirmUntil)
            {
                kickArmed = true;
            }
        }
        if (!kickArmed)
        {
            m_KickConfirmUntil = nowKick + 3000;
            SetCaption("BtnKickLabel", "#STR_LFPG_UI_CONFIRM_KICK");
            PaintRowButtons();
            return true;
        }

        m_KickConfirmUntil = 0;
        SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
        PaintRowButtons();
        SendMemberRPC(LFPG_RPC_C2S_REQUEST_KICK, m_MemberUID);
        return true;
    }

    // Envia via PlayerBase (funciona desde cualquier distancia)
    protected void SendMemberRPC(int rpcType, string targetUID)
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(targetUID);
        rpc.Send(player, rpcType, true, null);
    }
};

// ============================================================================
// LFPG_MemberRowView - ScriptView wrapper
// ============================================================================
class LFPG_MemberRowView extends ScriptView
{
    static const int NAME_ONLINE = ARGB(255, 255, 255, 255);
    static const int NAME_OFFLINE = ARGB(255, 118, 118, 118);
    override string GetLayoutFile()
    {
        return "SimpleGroup/gui/layouts/group_member_row.layout";
    }

    override typename GetControllerType()
    {
        return LFPG_MemberRowController;
    }

    override bool UseUpdateLoop()
    {
        return false;
    }

    LFPG_MemberRowController GetRowController()
    {
        return LFPG_MemberRowController.Cast(GetController());
    }

    // Panel tick. The controller owns the confirm window.
    void ExpireConfirm(int nowMs)
    {
        LFPG_MemberRowController rowCtrl = GetRowController();
        if (rowCtrl)
        {
            rowCtrl.ExpireConfirm(nowMs);
        }
    }

    void SetMemberData(string uid, string name, bool isLeader, bool isLocalPlayer, bool localIsLeader, bool isOnline)
    {
        LFPG_MemberRowController ctrl = GetRowController();
        if (ctrl)
        {
            ctrl.SetData(uid, name, isLeader, isLocalPlayer, localIsLeader, isOnline);
        }

        ApplyOnlineLook(isOnline);
    }

    // FIX I-20: Update in-place (mismo row reciclado, evita recrear widgets)
    void UpdateMemberData(string uid, string name, bool isLeader, bool isLocalPlayer, bool localIsLeader, bool isOnline)
    {
        LFPG_MemberRowController ctrl = GetRowController();
        if (ctrl)
        {
            ctrl.SetData(uid, name, isLeader, isLocalPlayer, localIsLeader, isOnline);
        }
        ApplyOnlineLook(isOnline);
    }

    // Name colour only. Offline names are grey.
    protected void ApplyOnlineLook(bool isOnline)
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        string nameTextName = "MemberNameText";
        TextWidget nameText = TextWidget.Cast(root.FindAnyWidget(nameTextName));
        if (!nameText)
            return;

        if (isOnline)
        {
            nameText.SetColor(NAME_ONLINE);
        }
        else
        {
            nameText.SetColor(NAME_OFFLINE);
        }
    }
};
