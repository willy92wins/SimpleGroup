// ============================================================================
// LFPG_DeployTracker.c - 3_Game
// Mapa estatico: entityKey -> groupID
//
// FIX D-4: Clave compuesta "low_high" desde GetNetworkID para evitar colisiones
//          de int32 GetID() en sesiones largas.
// FIX I-23: ClearByGroup(groupID) para limpiar tras DissolveGroup.
// FIX G-10: ClearAll() para Mission end en cliente.
//
// Registra items contados como mueble para decrement O(1) sin scan de posicion.
// Elimina double-decrement: Untrack borra la entrada, segunda llamada retorna "".
// Se repuebla automaticamente en cada recalibracion.
// ============================================================================

class LFPG_DeployTracker
{
    protected static ref map<string, string> s_Map;
    protected static ref map<string, string> s_GardenMap;

    // Helper para construir la clave compuesta
    static string MakeKey(int low, int high)
    {
        string k = low.ToString();
        k = k + "_";
        k = k + high.ToString();
        return k;
    }

    // Construir clave desde una entidad (usa GetNetworkID - 64bit)
    static string MakeKeyFromEntity(EntityAI ent)
    {
        if (!ent)
            return "";
        int lo = 0;
        int hi = 0;
        ent.GetNetworkID(lo, hi);
        return MakeKey(lo, hi);
    }

    // === DEPLOY (muebles) ===

    static void Track(EntityAI ent, string groupID)
    {
        if (!ent)
            return;
        if (!s_Map)
        {
            s_Map = new map<string, string>;
        }
        string key = MakeKeyFromEntity(ent);
        if (key == "")
            return;
        s_Map.Set(key, groupID);
    }

    // Desregistrar y retornar el groupID. Retorna "" si no estaba tracked.
    static string Untrack(EntityAI ent)
    {
        if (!ent || !s_Map)
            return "";

        string key = MakeKeyFromEntity(ent);
        if (key == "" || !s_Map.Contains(key))
            return "";

        string groupID = s_Map.Get(key);
        s_Map.Remove(key);
        return groupID;
    }

    // FIX C-3: Idempotencia — ya esta trackeado este item como deploy?
    static bool IsTracked(EntityAI ent)
    {
        if (!ent || !s_Map)
            return false;
        string key = MakeKeyFromEntity(ent);
        if (key == "")
            return false;
        return s_Map.Contains(key);
    }

    // === GARDEN (plots + greenhouses) ===

    static void TrackGarden(EntityAI ent, string groupID)
    {
        if (!ent)
            return;
        if (!s_GardenMap)
        {
            s_GardenMap = new map<string, string>;
        }
        string key = MakeKeyFromEntity(ent);
        if (key == "")
            return;
        s_GardenMap.Set(key, groupID);
    }

    static string UntrackGarden(EntityAI ent)
    {
        if (!ent || !s_GardenMap)
            return "";

        string key = MakeKeyFromEntity(ent);
        if (key == "" || !s_GardenMap.Contains(key))
            return "";

        string groupID = s_GardenMap.Get(key);
        s_GardenMap.Remove(key);
        return groupID;
    }

    // FIX C-3: Idempotencia para gardens
    static bool IsGardenTracked(EntityAI ent)
    {
        if (!ent || !s_GardenMap)
            return false;
        string key = MakeKeyFromEntity(ent);
        if (key == "")
            return false;
        return s_GardenMap.Contains(key);
    }

    // FIX I-23: Limpiar todas las entradas de un grupo (tras DissolveGroup)
    static void ClearByGroup(string groupID)
    {
        if (groupID == "")
            return;

        // Deploy map
        if (s_Map)
        {
            array<string> toRemove = new array<string>;
            int countD = s_Map.Count();
            int i;
            for (i = 0; i < countD; i = i + 1)
            {
                string keyD = s_Map.GetKey(i);
                string valD = s_Map.GetElement(i);
                if (valD == groupID)
                {
                    toRemove.Insert(keyD);
                }
            }
            int rmC = toRemove.Count();
            for (i = 0; i < rmC; i = i + 1)
            {
                s_Map.Remove(toRemove[i]);
            }
        }

        // Garden map
        if (s_GardenMap)
        {
            array<string> toRemoveG = new array<string>;
            int countG = s_GardenMap.Count();
            int j;
            for (j = 0; j < countG; j = j + 1)
            {
                string keyG = s_GardenMap.GetKey(j);
                string valG = s_GardenMap.GetElement(j);
                if (valG == groupID)
                {
                    toRemoveG.Insert(keyG);
                }
            }
            int rmGC = toRemoveG.Count();
            for (j = 0; j < rmGC; j = j + 1)
            {
                s_GardenMap.Remove(toRemoveG[j]);
            }
        }
    }

    // Limpiar ambos mapas (llamado antes de recalibracion para repoblar)
    static void Clear()
    {
        if (s_Map)
        {
            s_Map.Clear();
        }
        if (s_GardenMap)
        {
            s_GardenMap.Clear();
        }
    }

    // FIX G-10: Alias para claridad — clear en Mission end (cliente)
    static void ClearAll()
    {
        Clear();
    }
}
