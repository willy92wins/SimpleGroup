// ============================================================================
// LFPG_GroupPanel.c — 4_World/ui
// Panel de grupo (tecla P) — ScriptView sin bloqueo de input
//
// Dabs MVC:
//  - ScriptView crea widgets del layout automáticamente
//  - ViewController bindea propiedades a widgets
//  - ObservableCollection bindea member rows al WrapSpacer
//  - UseUpdateLoop = false (se actualiza solo por RPC)
//
// Ciclo de vida:
//  1. Pre-creado en MissionGameplay.OnInit (GetWorkspace válido)
//  2. Toggle con tecla P (show/hide, no crear/destruir)
//  3. Al show: solicitar GROUP_SYNC_FULL al server
//  4. Al recibir sync: refrescar datos en el controller
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
        // Limpiar rows existentes
        MemberRows.Clear();

        if (!LFPG_ClientGroupCache.s_Members)
            return;

        // Determinar si el jugador local es líder
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

            rowView.SetMemberData(
                member.m_PlayerUID,
                member.m_PlayerName,
                isLeader,
                isSelf,
                localIsLeader
            );

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
    bool OnLeaveExecute(ButtonCommandArgs args)
    {
        // Buscar bandera para enviar RPC
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return false;

        LFPG_FlagBase flag = FindNearestGroupFlag(player);
        if (!flag)
            return false;

        ScriptRPC rpc = new ScriptRPC();
        rpc.Send(flag, LFPG_RPC_C2S_REQUEST_LEAVE, true, null);

        // Cerrar panel
        LFPG_GroupPanel panel = LFPG_GroupPanel.GetInstance();
        if (panel)
        {
            panel.Hide();
        }

        return true;
    }

    protected LFPG_FlagBase FindNearestGroupFlag(PlayerBase player)
    {
        vector playerPos = player.GetPosition();
        float searchRadius = 100.0;
        array<Object> objects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition(playerPos, searchRadius, objects, proxyCargos);

        int i;
        int count = objects.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(objects[i]);
            if (flag && flag.GetGroupID() == LFPG_ClientGroupCache.s_GroupID)
            {
                return flag;
            }
        }
        return null;
    }
};

// ============================================================================
// LFPG_GroupPanel — ScriptView principal
// ============================================================================
class LFPG_GroupPanel extends ScriptView
{
    // Singleton — pre-creado en MissionGameplay.OnInit
    protected static ref LFPG_GroupPanel s_Instance;

    protected bool m_IsVisible;

    void LFPG_GroupPanel()
    {
        m_IsVisible = false;
        // Empezar oculto
        Widget root = GetLayoutRoot();
        if (root)
        {
            root.Show(false);
        }
    }

    void ~LFPG_GroupPanel()
    {
        s_Instance = null;
    }

    override string GetLayoutFile()
    {
        return "LFPG_Territory/gui/layouts/group_panel.layout";
    }

    override typename GetControllerType()
    {
        return LFPG_GroupPanelController;
    }

    override bool UseUpdateLoop()
    {
        return false;
    }

    // ========================================================================
    // SINGLETON
    // ========================================================================
    static void CreateInstance()
    {
        if (s_Instance)
            return;

        // Solo crear si GetWorkspace es válido (Rule 6 y 9)
        if (!GetGame())
            return;
        if (!GetGame().GetWorkspace())
            return;

        s_Instance = new LFPG_GroupPanel();
    }

    static void DestroyInstance()
    {
        s_Instance = null;
    }

    static LFPG_GroupPanel GetInstance()
    {
        return s_Instance;
    }

    // ========================================================================
    // TOGGLE
    // ========================================================================
    static void Toggle()
    {
        if (!s_Instance)
            return;

        if (s_Instance.m_IsVisible)
        {
            s_Instance.Hide();
        }
        else
        {
            s_Instance.Show();
        }
    }

    void Show()
    {
        if (m_IsVisible)
            return;

        if (!LFPG_ClientGroupCache.HasGroup())
            return;

        m_IsVisible = true;
        Widget root = GetLayoutRoot();
        if (root)
        {
            root.Show(true);
        }

        // Refrescar datos del cache
        LFPG_GroupPanelController ctrl = LFPG_GroupPanelController.Cast(GetController());
        if (ctrl)
        {
            ctrl.RefreshFromCache();
        }

        // Solicitar sync fresco al server
        RequestGroupData();
    }

    void Hide()
    {
        if (!m_IsVisible)
            return;

        m_IsVisible = false;
        Widget root = GetLayoutRoot();
        if (root)
        {
            root.Show(false);
        }
    }

    bool IsVisible()
    {
        return m_IsVisible;
    }

    // Llamado cuando llega nuevo sync del server
    void OnDataReceived()
    {
        if (!m_IsVisible)
            return;

        LFPG_GroupPanelController ctrl = LFPG_GroupPanelController.Cast(GetController());
        if (ctrl)
        {
            ctrl.RefreshFromCache();
        }
    }

    // Solicitar datos actualizados al server
    protected void RequestGroupData()
    {
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return;

        // Buscar la bandera del grupo para enviar el RPC
        vector playerPos = player.GetPosition();
        float searchRadius = 100.0;
        array<Object> objects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;
        GetGame().GetObjectsAtPosition(playerPos, searchRadius, objects, proxyCargos);

        int i;
        int count = objects.Count();
        for (i = 0; i < count; i = i + 1)
        {
            LFPG_FlagBase flag = LFPG_FlagBase.Cast(objects[i]);
            if (flag && flag.GetGroupID() == LFPG_ClientGroupCache.s_GroupID)
            {
                ScriptRPC rpc = new ScriptRPC();
                rpc.Send(flag, LFPG_RPC_C2S_REQUEST_GROUP_DATA, true, null);
                return;
            }
        }
    }
};
