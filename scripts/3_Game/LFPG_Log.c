// ============================================================================
// LFPG_Log.c - 3_Game
// Sistema de log centralizado con niveles
// Info: mensajes operacionales (siempre visibles)
// Debug: mensajes verbose (solo en DEVELOPER builds / Workbench)
// Error: errores y warnings criticos (siempre visibles, prefijo ERR)
// ============================================================================

class LFPG_Log
{
    static void Info(string msg)
    {
        string full = "[SimpleGroup] ";
        full = full + msg;
        Print(full);
    }

    static void Debug(string msg)
    {
        #ifdef DEVELOPER
        string full = "[SimpleGroup:DBG] ";
        full = full + msg;
        Print(full);
        #endif
    }

    static void Error(string msg)
    {
        string full = "[SimpleGroup:ERR] ";
        full = full + msg;
        Print(full);
    }
};
