// ============================================================================
// LFPG_MemberRowView.c - 4_World/ui
// One member row in the group panel. Dabs ScriptView plus ViewController.
//
// Layout bindings:
//  - LeaderTag: localized "Leader" on the leader row, empty otherwise
//  - MemberName: player name, with the localized "(you)" on the local row
//  - BtnTransfer: visible to the leader, hidden on the leader's own row
//  - BtnKick: visible to the leader, hidden on the leader's own row
// ============================================================================

class LFPG_MemberRowController extends ViewController
{
    // Bound properties (nombres deben coincidir con Binding_Name del layout)
    string LeaderTag;
    string MemberName;

    static const int COLOR_TRANSFER = ARGB(255, 43, 48, 54);
    static const int COLOR_TRANSFER_ARMED = ARGB(255, 107, 84, 24);
    static const int COLOR_KICK = ARGB(255, 74, 31, 29);
    static const int COLOR_KICK_ARMED = ARGB(255, 138, 42, 36);

    // Datos internos (no bindeados)
    string m_MemberUID;
    bool m_IsLeader;
    bool m_IsSelf;
    bool m_IsOnline;

    // Second click must arrive before this mission time (ms). 0 means not armed.
    protected int m_KickConfirmUntil;
    protected int m_TransferConfirmUntil;

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
            string nameTextName = "MemberNameText";
            TextWidget nameText = TextWidget.Cast(m_LayoutRoot.FindAnyWidget(nameTextName));
            if (nameText)
            {
                if (showButtons)
                {
                    nameText.SetSize(106.0, 34.0);
                }
                else
                {
                    nameText.SetSize(190.0, 34.0);
                }
            }
        }

        // Indicador online/offline
        m_IsOnline = isOnline;

        // A recycled row must not keep the previous member's confirm.
        if (!sameMember)
        {
            m_KickConfirmUntil = 0;
            m_TransferConfirmUntil = 0;
            SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
            SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
            PaintMemberButton(BtnKick, COLOR_KICK);
            PaintMemberButton(BtnTransfer, COLOR_TRANSFER);
        }
    }

    protected void PaintMemberButton(ButtonWidget button, int color)
    {
        if (!button)
            return;

        button.SetColor(color);
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
            PaintMemberButton(BtnTransfer, COLOR_TRANSFER_ARMED);
            return true;
        }

        m_TransferConfirmUntil = 0;
        SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
        PaintMemberButton(BtnTransfer, COLOR_TRANSFER);
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
                PaintMemberButton(BtnKick, COLOR_KICK);
            }
        }
        if (m_TransferConfirmUntil > 0)
        {
            if (nowMs > m_TransferConfirmUntil)
            {
                m_TransferConfirmUntil = 0;
                SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
                PaintMemberButton(BtnTransfer, COLOR_TRANSFER);
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
            PaintMemberButton(BtnKick, COLOR_KICK_ARMED);
            return true;
        }

        m_KickConfirmUntil = 0;
        SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
        PaintMemberButton(BtnKick, COLOR_KICK);
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
    static const int COLOR_ONLINE_DOT = ARGB(255, 80, 200, 80);
    static const int COLOR_OFFLINE_DOT = ARGB(255, 95, 95, 95);
    static const int COLOR_NAME_ONLINE = ARGB(255, 224, 224, 224);
    static const int COLOR_NAME_OFFLINE = ARGB(255, 125, 125, 125);
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

    // Online dot and name colour. The layout paints the row background.
    protected void ApplyOnlineLook(bool isOnline)
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        string nameIndicator = "OnlineIndicator";
        ImageWidget indicator = ImageWidget.Cast(root.FindAnyWidget(nameIndicator));
        string nameTextName = "MemberNameText";
        TextWidget nameText = TextWidget.Cast(root.FindAnyWidget(nameTextName));
        if (isOnline)
        {
            if (indicator)
            {
                indicator.SetColor(COLOR_ONLINE_DOT);
            }
            if (nameText)
            {
                nameText.SetColor(COLOR_NAME_ONLINE);
            }
        }
        else
        {
            if (indicator)
            {
                indicator.SetColor(COLOR_OFFLINE_DOT);
            }
            if (nameText)
            {
                nameText.SetColor(COLOR_NAME_OFFLINE);
            }
        }
    }
};
