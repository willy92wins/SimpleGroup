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

    // Datos internos
    string m_GroupID;
    LFPG_FlagBase m_TargetFlag;

    void LFPG_GroupNameDialogController()
    {
        EditGroupName = "";
        ErrorMessage = "";
        m_GroupID = "";
    }

    void SetContext(string groupID, LFPG_FlagBase flag)
    {
        m_GroupID = groupID;
        m_TargetFlag = flag;
    }

    // Relay_Command: confirmar nombre
    bool OnConfirmExecute(ButtonCommandArgs args)
    {
        string name = EditGroupName;

        // Validación client-side (preview, no autoritativa)
        int nameLen = name.Length();
        if (nameLen < 3)
        {
            ErrorMessage = "#STR_LFPG_ERR_NAME_SHORT";
            string propErr = "ErrorMessage";
            NotifyPropertyChanged(propErr);
            return true;
        }
        if (nameLen > 24)
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
        return {"menu"};
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
            Print("[SimpleGroup] ERROR: GroupNameDialog layout failed to load. Cleaning up.");
            s_Instance = null;
            dialog = null;
            return;
        }

        LFPG_GroupNameDialogController ctrl = LFPG_GroupNameDialogController.Cast(dialog.GetController());
        if (ctrl)
        {
            ctrl.SetContext(groupID, flag);
        }
    }

    void CloseDialog()
    {
        Close();
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
