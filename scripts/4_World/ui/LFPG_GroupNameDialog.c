// ============================================================================
// LFPG_GroupNameDialog.c — 4_World/ui
// ScriptViewMenu para ingresar nombre de grupo
// Se abre cuando el server envía S2C_OPEN_NAME_DIALOG tras colocar bandera
//
// ScriptViewMenu: bloquea input automáticamente, muestra cursor
// Botones via Relay_Command (Pattern 1 — función en controller)
// EditBox con Two_Way_Binding para capturar texto
// ============================================================================

class LFPG_GroupNameDialogController extends ViewController
{
    // Bound properties (Binding_Name en layout)
    string EditGroupName;
    string ErrorMessage;

    static const int BTN_IDLE = ARGB(0, 0, 0, 0);
    static const int BTN_HOT = ARGB(255, 255, 0, 0);

    // Datos internos
    string m_GroupID;
    LFPG_FlagBase m_TargetFlag;

    protected bool m_CancelHover;
    protected bool m_ConfirmHover;
    protected int m_NextConfirmAt;

    void LFPG_GroupNameDialogController()
    {
        EditGroupName = "";
        ErrorMessage = "";
        m_GroupID = "";
        m_CancelHover = false;
        m_ConfirmHover = false;
    }

    // FIX I-19: Destructor limpia m_TargetFlag para evitar ref colgando
    void ~LFPG_GroupNameDialogController()
    {
        m_TargetFlag = null;
    }

    void SetContext(string groupID, LFPG_FlagBase flag)
    {
        m_GroupID = groupID;
        m_TargetFlag = flag;
    }

    // Relay_Command: confirmar nombre
    bool OnConfirmExecute(ButtonCommandArgs args)
    {
        if (!GetGame())
            return false;
        int now = GetGame().GetTime();
        if (now < m_NextConfirmAt)
            return true;
        string name = EditGroupName;
        name.TrimInPlace();

        // Validación client-side (preview, no autoritativa).
        // AUDIT #10 F20: las cotas reales son las de la config del server
        // (1-48, min <= max) y el server ya responde TOO_SHORT/TOO_LONG. El
        // 3/24 fijo rechazaba nombres validos; aqui solo se filtra lo imposible.
        int nameLen = name.Length();
        if (nameLen < 1)
        {
            ErrorMessage = "#STR_LFPG_ERR_NAME_SHORT";
            string propErr = "ErrorMessage";
            NotifyPropertyChanged(propErr);
            return true;
        }
        if (nameLen > 48)
        {
            ErrorMessage = "#STR_LFPG_ERR_NAME_LONG";
            string propErr2 = "ErrorMessage";
            NotifyPropertyChanged(propErr2);
            return true;
        }

        // Enviar al server para validación autoritativa
        if (!m_TargetFlag)
            return false;

        ScriptRPC rpc = new ScriptRPC();
        // Ignore double clicks within the server throttle window. A timeout
        // permits retry even if the server answers with a generic error.
        m_NextConfirmAt = now + 500;
        rpc.Write(name);
        rpc.Send(m_TargetFlag, LFPG_RPC_C2S_SET_GROUP_NAME, true, null);

        // Limpiar error mientras esperamos respuesta
        ErrorMessage = "";
        string propClear = "ErrorMessage";
        NotifyPropertyChanged(propClear);

        return true;
    }

    // Relay_Command: cancelar (cierra el diálogo, grupo queda con nombre temporal)
    bool OnCancelExecute(ButtonCommandArgs args)
    {
        LFPG_GroupNameDialog dialog = LFPG_GroupNameDialog.GetInstance();
        if (dialog)
        {
            dialog.CloseDialog();
        }
        return true;
    }

    // Cancel and Confirm have no confirm-arm. The pointer alone fills the panel.
    protected void PaintDialogButtons()
    {
        if (!m_LayoutRoot)
            return;

        string cancelPanelName = "BtnCancelPanel";
        Widget cancelPanel = m_LayoutRoot.FindAnyWidget(cancelPanelName);
        if (cancelPanel)
        {
            if (m_CancelHover)
            {
                cancelPanel.SetColor(BTN_HOT);
            }
            else
            {
                cancelPanel.SetColor(BTN_IDLE);
            }
        }

        string confirmPanelName = "BtnConfirmPanel";
        Widget confirmPanel = m_LayoutRoot.FindAnyWidget(confirmPanelName);
        if (confirmPanel)
        {
            if (m_ConfirmHover)
            {
                confirmPanel.SetColor(BTN_HOT);
            }
            else
            {
                confirmPanel.SetColor(BTN_IDLE);
            }
        }
    }

    override bool OnMouseEnter(Widget w, int x, int y)
    {
        if (!w)
            return false;

        string enteredName = w.GetName();
        if (enteredName == "BtnCancel")
        {
            m_CancelHover = true;
            PaintDialogButtons();
        }
        else if (enteredName == "BtnConfirm")
        {
            m_ConfirmHover = true;
            PaintDialogButtons();
        }
        return false;
    }

    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        if (!w)
            return false;

        string leftName = w.GetName();
        if (leftName == "BtnCancel")
        {
            m_CancelHover = false;
            PaintDialogButtons();
        }
        else if (leftName == "BtnConfirm")
        {
            m_ConfirmHover = false;
            PaintDialogButtons();
        }
        return false;
    }

    // Llamado cuando el server responde con NAME_RESULT
    void OnNameResult(int result)
    {
        if (result == LFPG_NAME_OK)
        {
            // Éxito — cerrar diálogo
            LFPG_GroupNameDialog dialog = LFPG_GroupNameDialog.GetInstance();
            if (dialog)
            {
                dialog.CloseDialog();
            }
        }
        else
        {
            // Error — mostrar mensaje
            if (result == LFPG_NAME_TOO_SHORT)
            {
                ErrorMessage = "#STR_LFPG_ERR_NAME_SHORT";
            }
            else if (result == LFPG_NAME_TOO_LONG)
            {
                ErrorMessage = "#STR_LFPG_ERR_NAME_LONG";
            }
            else if (result == LFPG_NAME_TAKEN)
            {
                ErrorMessage = "#STR_LFPG_ERR_NAME_TAKEN";
            }
            else if (result == LFPG_NAME_INVALID_CHARS)
            {
                ErrorMessage = "#STR_LFPG_ERR_NAME_INVALID";
            }

            string propErr = "ErrorMessage";
            NotifyPropertyChanged(propErr);
        }
    }
};

// ============================================================================
// LFPG_GroupNameDialog — ScriptViewMenu
// ============================================================================
class LFPG_GroupNameDialog extends ScriptViewMenu
{
    protected static ref LFPG_GroupNameDialog s_Instance;

    // Close() deletes on a later GUI tick. A second Close in that window double-frees.
    protected bool m_CloseRequested;

    void LFPG_GroupNameDialog()
    {
        s_Instance = this;
    }

    void ~LFPG_GroupNameDialog()
    {
        if (s_Instance == this)
        {
            s_Instance = null;
        }
    }

    override string GetLayoutFile()
    {
        return "SimpleGroup/gui/layouts/group_name_dialog.layout";
    }

    override typename GetControllerType()
    {
        return LFPG_GroupNameDialogController;
    }

    override bool UseUpdateLoop()
    {
        return false;
    }

    // ScriptViewMenu overrides
    override bool UseMouse()
    {
        return true;
    }

    override bool UseKeyboard()
    {
        return true;
    }

    override array<string> GetInputExcludes()
    {
        // FIX I-17: bloquear acciones combat/inventory mientras escribes nombre
        return {"menu", "inventory", "firearm", "melee"};
    }

    override bool CanCloseWithEscape()
    {
        return true;
    }

    // ========================================================================
    // SINGLETON / OPEN / CLOSE
    // ========================================================================
    static LFPG_GroupNameDialog GetInstance()
    {
        return s_Instance;
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

    static void Open(string groupID, LFPG_FlagBase flag)
    {
        // Crear nuevo diálogo (ScriptViewMenu se registra con UIManager)
        if (s_Instance)
            return;

        if (!GetGame() || !GetGame().GetWorkspace())
            return;

        LFPG_GroupNameDialog dialog = new LFPG_GroupNameDialog();

        // Guard: si el layout no se creó (ruta inválida, etc), limpiar
        if (!dialog.GetLayoutRoot())
        {
            LFPG_Log.Error("GroupNameDialog layout failed to load. Cleaning up.");
            s_Instance = null;
            dialog = null;
            return;
        }

        dialog.GetLayoutRoot().Show(true);

        LFPG_GroupNameDialogController ctrl = LFPG_GroupNameDialogController.Cast(dialog.GetController());
        if (ctrl)
        {
            ctrl.SetContext(groupID, flag);
        }
    }

    void CloseDialog()
    {
        RequestCloseOnce();
    }

    // Llamado por el ClientGroupCache cuando llega NAME_RESULT
    static void HandleNameResult(int result)
    {
        if (!s_Instance)
            return;

        LFPG_GroupNameDialogController ctrl = LFPG_GroupNameDialogController.Cast(s_Instance.GetController());
        if (ctrl)
        {
            ctrl.OnNameResult(result);
        }
    }
};
