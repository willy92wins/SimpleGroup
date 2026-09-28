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
    }

    void SetData(string uid, string name, bool isLeader, bool isLocalPlayer, bool localIsLeader, bool isOnline)
    {
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
    }

    // Relay_Command: transferir liderazgo a este miembro
    bool OnTransferExecute(ButtonCommandArgs args)
    {
        if (m_MemberUID == "")
            return false;

        SendMemberRPC(LFPG_RPC_C2S_REQUEST_TRANSFER, m_MemberUID);
        return true;
    }

    // Relay_Command: expulsar a este miembro
    bool OnKickExecute(ButtonCommandArgs args)
    {
        if (m_MemberUID == "")
            return false;

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
