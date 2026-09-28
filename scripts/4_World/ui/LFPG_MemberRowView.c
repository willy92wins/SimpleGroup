// ============================================================================
// LFPG_MemberRowView.c - 4_World/ui
// ScriptView para cada fila de miembro en el panel de grupo
// Usa Dabs MVC: ScriptView + ViewController con ViewBindings
//
// Bindings del layout:
//  - LeaderStar: TextWidget (* si es lider, vacio si no)
//  - MemberName: TextWidget (nombre del jugador)
//  - BtnTransfer: visible solo para lider, no en su propia fila
//  - BtnKick: visible solo para lider, no en su propia fila
// ============================================================================

class LFPG_MemberRowController extends ViewController
{
    // Bound properties (nombres deben coincidir con Binding_Name del layout)
    string LeaderStar;
    string MemberName;

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
        LeaderStar = "";
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

        // Estrella de lider
        if (isLeader)
        {
            LeaderStar = "*";
        }
        else
        {
            LeaderStar = "";
        }

        MemberName = name;

        string propStar = "LeaderStar";
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

        // Indicador online/offline
        m_IsOnline = isOnline;

        // A recycled row must not keep the previous member's confirm.
        if (!sameMember)
        {
            m_KickConfirmUntil = 0;
            m_TransferConfirmUntil = 0;
            SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
            SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
        }
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
            return true;
        }

        m_TransferConfirmUntil = 0;
        SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
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
            }
        }
        if (m_TransferConfirmUntil > 0)
        {
            if (nowMs > m_TransferConfirmUntil)
            {
                m_TransferConfirmUntil = 0;
                SetCaption("BtnTransferLabel", "#STR_LFPG_UI_TRANSFER");
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
            return true;
        }

        m_KickConfirmUntil = 0;
        SetCaption("BtnKickLabel", "#STR_LFPG_UI_KICK");
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

        // FIX AUDIT: Forzar carga de imagen procedural en RowBg y OnlineIndicator
        InitRowBackground();
        InitOnlineIndicator(isOnline);
    }

    // FIX I-20: Update in-place (mismo row reciclado, evita recrear widgets)
    void UpdateMemberData(string uid, string name, bool isLeader, bool isLocalPlayer, bool localIsLeader, bool isOnline)
    {
        LFPG_MemberRowController ctrl = GetRowController();
        if (ctrl)
        {
            ctrl.SetData(uid, name, isLeader, isLocalPlayer, localIsLeader, isOnline);
        }
        // Indicator puede cambiar online/offline
        InitOnlineIndicator(isOnline);
    }

    // Indicador online: verde = online, gris = offline
    protected void InitOnlineIndicator(bool isOnline)
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        string colorTex = "#(argb,8,8,3)color(1,1,1,1,CO)";
        string nameIndicator = "OnlineIndicator";
        ImageWidget indicator = ImageWidget.Cast(root.FindAnyWidget(nameIndicator));
        if (indicator)
        {
            indicator.LoadImageFile(0, colorTex);
            if (isOnline)
            {
                indicator.SetColor(ARGB(255, 80, 200, 80));
            }
            else
            {
                indicator.SetColor(ARGB(255, 100, 100, 100));
            }
        }
    }

    // FIX AUDIT: ImageWidget necesita LoadImageFile() + SetColor() para colores procedurales
    protected void InitRowBackground()
    {
        Widget root = GetLayoutRoot();
        if (!root)
            return;

        string colorTex = "#(argb,8,8,3)color(1,1,1,1,CO)";
        string nameBg = "RowBg";
        ImageWidget rowBg = ImageWidget.Cast(root.FindAnyWidget(nameBg));
        if (rowBg)
        {
            // Layout color 0.15 0.15 0.18 0.5 → ARGB(128, 38, 38, 46)
            rowBg.LoadImageFile(0, colorTex);
            rowBg.SetColor(ARGB(128, 38, 38, 46));
        }
    }
};
