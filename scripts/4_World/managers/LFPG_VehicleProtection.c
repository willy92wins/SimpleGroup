// ============================================================================
// LFPG_VehicleProtection.c - 4_World/managers
// Vehicle protection under group flags (config v6). Server only.
//
// Each group keeps the keys of the vehicles inside its build radius, in the
// order they entered it. While the group flag is raised, the first N vehicles
// found in the world (N from the flag tier) keep at least m_VehicleLifetime
// seconds of lifetime and show the persistence icon. Other vehicles keep
// their own lifetime.
//
// A key is the persistent id "b1:b2:b3:b4", read from the live vehicle when a
// scan finds it. Keys are never resolved while saving.
//
// The queues are saved to $profile:SimpleGroup/vehicles.json. The save is
// recoverable, not atomic: tmp -> verify -> backup -> copy -> verify.
// ============================================================================

class LFPG_VehicleQueueRecord
{
    string m_GroupID;
    ref array<string> m_Vehicles;
};

class LFPG_VehicleQueuesData
{
    int m_Version;
    ref array<ref LFPG_VehicleQueueRecord> m_Queues;
};

class LFPG_VehicleProtection
{
    static const int FILE_VERSION = 1;
    // Flags scanned per validation tick. Protection covers every queue each tick.
    static const int SCAN_BATCH = 20;
    // A queued vehicle keeps its place up to this far beyond the build radius.
    static const float EXIT_MARGIN = 2.0;
    // Consecutive scans that do not find a queued vehicle before its key is dropped.
    static const int MAX_MISSES = 3;
    // Entries dropped while loading that are named in the log; the rest are counted.
    static const int DROP_LOG_LIMIT = 20;

    // groupID -> vehicle keys in order of arrival. Saved to vehicles.json.
    protected ref map<string, ref array<string>> m_Queues;
    // key -> groupID of the queue that holds it. A key is in one queue at most.
    protected ref map<string, string> m_KeyGroup;
    // key -> vehicle found by the last scan that saw it. Runtime only.
    protected ref map<string, Transport> m_Bound;
    // key -> consecutive scans that did not find the vehicle. Runtime only.
    protected ref map<string, int> m_Misses;
    // key -> a lifetime below the protected floor was already logged. Runtime only.
    protected ref map<string, bool> m_FloorLogged;
    // groupID -> flag position at the last exit pass. A flag moved by admin tools
    // does not cut the vehicles left outside its zone: they did not move (D5).
    protected ref map<string, vector> m_LastCenter;

    protected bool m_Loaded;
    // A newer vehicles file exists, or the final cannot be opened: queues live in
    // memory and nothing is written this session.
    protected bool m_ReadOnly;
    protected bool m_Dirty;
    protected bool m_SaveErrorLogged;
    protected bool m_TmpErrorLogged;
    // The invalid final of the current failing save streak was already copied aside.
    protected bool m_CorruptCopied;

    // Reused buffers.
    protected ref array<Object> m_ScanObjects;
    protected ref array<CargoBase> m_ScanCargo;
    protected ref map<string, Transport> m_Candidates;
    protected ref array<string> m_CandidateKeys;
    protected ref array<string> m_GroupBuffer;
    protected ref array<string> m_SlotKeys;
    protected ref array<string> m_SlotGroups;

    void LFPG_VehicleProtection()
    {
        m_Queues = new map<string, ref array<string>>;
        m_KeyGroup = new map<string, string>;
        m_Bound = new map<string, Transport>;
        m_Misses = new map<string, int>;
        m_FloorLogged = new map<string, bool>;
        m_LastCenter = new map<string, vector>;
        m_Loaded = false;
        m_ReadOnly = false;
        m_Dirty = false;
        m_SaveErrorLogged = false;
        m_TmpErrorLogged = false;
        m_CorruptCopied = false;
        m_ScanObjects = new array<Object>;
        m_ScanCargo = new array<CargoBase>;
        m_Candidates = new map<string, Transport>;
        m_CandidateKeys = new array<string>;
        m_GroupBuffer = new array<string>;
        m_SlotKeys = new array<string>;
        m_SlotGroups = new array<string>;
    }

    static string GetFilePath()
    {
        return "$profile:SimpleGroup/vehicles.json";
    }

    static string GetTmpPath()
    {
        return "$profile:SimpleGroup/vehicles.json.tmp";
    }

    static string GetBackupPath()
    {
        return "$profile:SimpleGroup/vehicles.json.bak";
    }

    bool IsLoaded()
    {
        return m_Loaded;
    }

    // ========================================================================
    // KEYS
    // ========================================================================

    // Persistent id as "b1:b2:b3:b4"; empty while the vehicle has none.
    static string KeyOf(Transport vehicle)
    {
        if (!vehicle)
            return "";
        int b1;
        int b2;
        int b3;
        int b4;
        vehicle.GetPersistentID(b1, b2, b3, b4);
        if (b1 == 0 && b2 == 0 && b3 == 0 && b4 == 0)
            return "";
        return JoinKey(b1, b2, b3, b4);
    }

    protected static string JoinKey(int b1, int b2, int b3, int b4)
    {
        string joined = b1.ToString();
        joined = joined + ":" + b2.ToString();
        joined = joined + ":" + b3.ToString();
        joined = joined + ":" + b4.ToString();
        return joined;
    }

    // A key read from disk must have exactly the form KeyOf produces.
    static bool IsValidKey(string key)
    {
        if (key == "")
            return false;
        array<string> parts = new array<string>;
        key.Split(":", parts);
        if (parts.Count() != 4)
            return false;
        string s1 = parts[0];
        string s2 = parts[1];
        string s3 = parts[2];
        string s4 = parts[3];
        int p1 = s1.ToInt();
        int p2 = s2.ToInt();
        int p3 = s3.ToInt();
        int p4 = s4.ToInt();
        if (p1 == 0 && p2 == 0 && p3 == 0 && p4 == 0)
            return false;
        return JoinKey(p1, p2, p3, p4) == key;
    }

    protected static float DistanceSqXZ(vector a, vector b)
    {
        float dx = a[0] - b[0];
        float dz = a[2] - b[2];
        return (dx * dx) + (dz * dz);
    }

    // ========================================================================
    // LOAD
    // ========================================================================

    // Final, then tmp, then backup. With no usable file the queues start empty
    // and the next scans rebuild them in scan order.
    void Load()
    {
        m_Loaded = true;
        m_ReadOnly = false;
        m_Dirty = false;
        m_Queues.Clear();
        m_KeyGroup.Clear();
        m_Bound.Clear();
        m_Misses.Clear();
        m_FloorLogged.Clear();
        m_LastCenter.Clear();

        string finalPath = GetFilePath();
        string tmpPath = GetTmpPath();
        string bakPath = GetBackupPath();

        // A newer format in any of the three files: read only. A valid version 1
        // file is still loaded below, into memory.
        if (CheckFutureFiles())
        {
            m_ReadOnly = true;
            LFPG_Log.Error("Vehicle queues are kept in memory and no vehicles file is written this session.");
        }

        // A final that exists but cannot be opened (held by another process) may be
        // newer than tmp and the backup: it is never replaced this session.
        if (FileExist(finalPath) && LFPG_GroupsStorage.ReadByteCount(finalPath) < 0)
        {
            m_ReadOnly = true;
            LFPG_Log.Error("vehicles.json exists but cannot be opened. Vehicle queues are kept in memory and no vehicles file is written this session.");
        }

        LFPG_VehicleQueuesData loaded;
        string loadError = "";
        if (FileExist(finalPath))
        {
            if (ReadQueuesFile(finalPath, loaded, loadError))
            {
                ApplyLoadedData(loaded, "vehicles.json");
                return;
            }
            string finalErr = "vehicles.json unusable (";
            finalErr = finalErr + loadError;
            finalErr = finalErr + "); trying vehicles.json.tmp and vehicles.json.bak.";
            LFPG_Log.Error(finalErr);
        }

        // A recovered copy is written back as the final on the first save.
        if (FileExist(tmpPath) && ReadQueuesFile(tmpPath, loaded, loadError))
        {
            ApplyLoadedData(loaded, "vehicles.json.tmp");
            m_Dirty = true;
            return;
        }
        if (FileExist(bakPath) && ReadQueuesFile(bakPath, loaded, loadError))
        {
            ApplyLoadedData(loaded, "vehicles.json.bak");
            m_Dirty = true;
            return;
        }

        if (FileExist(finalPath) || FileExist(tmpPath) || FileExist(bakPath))
            LFPG_Log.Error("No usable vehicles file. Vehicle queues start empty and are rebuilt by scan.");
        else
            LFPG_Log.Info("vehicles.json not found. Vehicle queues start empty.");
    }

    protected bool IsFutureFile(string path)
    {
        return LFPG_GroupsStorage.ReadVersion(path) > FILE_VERSION;
    }

    // Logs each vehicles file written by a newer build; true when there is one.
    protected bool CheckFutureFiles()
    {
        bool anyFuture = false;
        if (IsFutureFile(GetFilePath()))
        {
            anyFuture = true;
            LFPG_Log.Error("vehicles.json has a newer format than this build reads.");
        }
        if (IsFutureFile(GetTmpPath()))
        {
            anyFuture = true;
            LFPG_Log.Error("vehicles.json.tmp has a newer format than this build reads.");
        }
        if (IsFutureFile(GetBackupPath()))
        {
            anyFuture = true;
            LFPG_Log.Error("vehicles.json.bak has a newer format than this build reads.");
        }
        return anyFuture;
    }

    // Parses and checks one vehicles file. Entries are checked by ApplyLoadedData.
    protected bool ReadQueuesFile(string path, out LFPG_VehicleQueuesData data, out string error)
    {
        data = null;
        error = "";
        string raw;
        if (!LFPG_GroupsStorage.ReadText(path, raw))
        {
            error = "unreadable or outside size bound";
            return false;
        }
        LFPG_VehicleQueuesData staged = new LFPG_VehicleQueuesData();
        if (!JsonFileLoader<LFPG_VehicleQueuesData>.LoadData(raw, staged, error))
            return false;
        if (staged.m_Version != FILE_VERSION)
        {
            error = "unsupported version " + staged.m_Version.ToString();
            return false;
        }
        array<string> topKeys;
        if (!LFPG_TerritoryConfig.FindTopLevelKeys(raw, topKeys) || topKeys.Find("m_Queues") < 0 || !staged.m_Queues)
        {
            error = "missing m_Queues";
            return false;
        }
        data = staged;
        return true;
    }

    // Malformed keys, repeated keys and repeated groups are dropped; the first entry
    // wins. The first DROP_LOG_LIMIT drops are named in the log.
    protected void ApplyLoadedData(LFPG_VehicleQueuesData data, string source)
    {
        int dropped = 0;
        int vehicleCount = 0;
        int recordCount = data.m_Queues.Count();
        int ri;
        for (ri = 0; ri < recordCount; ri = ri + 1)
        {
            LFPG_VehicleQueueRecord record = data.m_Queues[ri];
            if (!record || record.m_GroupID == "" || !record.m_Vehicles || m_Queues.Contains(record.m_GroupID))
            {
                dropped = dropped + 1;
                string recordWhat = "record " + ri.ToString();
                if (record)
                    recordWhat = recordWhat + " group=" + record.m_GroupID;
                LogDropped(recordWhat, dropped);
                continue;
            }
            array<string> loadedQueue = new array<string>;
            int keyCount = record.m_Vehicles.Count();
            int ki;
            for (ki = 0; ki < keyCount; ki = ki + 1)
            {
                string loadedKey = record.m_Vehicles[ki];
                if (!IsValidKey(loadedKey) || m_KeyGroup.Contains(loadedKey))
                {
                    dropped = dropped + 1;
                    string keyWhat = "key " + loadedKey;
                    keyWhat = keyWhat + " group=" + record.m_GroupID;
                    LogDropped(keyWhat, dropped);
                    continue;
                }
                loadedQueue.Insert(loadedKey);
                m_KeyGroup.Set(loadedKey, record.m_GroupID);
                vehicleCount = vehicleCount + 1;
            }
            if (loadedQueue.Count() > 0)
                m_Queues.Set(record.m_GroupID, loadedQueue);
        }

        string loadMsg = "Vehicle queues loaded from ";
        loadMsg = loadMsg + source;
        if (m_ReadOnly)
            loadMsg = loadMsg + " (read only)";
        loadMsg = loadMsg + ": groups=" + m_Queues.Count().ToString();
        loadMsg = loadMsg + " vehicles=" + vehicleCount.ToString();
        loadMsg = loadMsg + " dropped=" + dropped.ToString();
        if (dropped > 0)
            LFPG_Log.Error(loadMsg);
        else
            LFPG_Log.Info(loadMsg);
    }

    protected void LogDropped(string what, int droppedSoFar)
    {
        if (droppedSoFar > DROP_LOG_LIMIT)
            return;
        LFPG_Log.Error("Vehicle queue entry dropped at load: " + what);
    }

    // ========================================================================
    // SCAN - one registered flag per call, batched by the group manager
    // ========================================================================

    // Binds queued vehicles found near the flag, counts misses for the others
    // and appends vehicles that entered the build radius.
    void ScanFlag(LFPG_FlagBase flag, string groupID, LFPG_GroupManager mgr, LFPG_TerritoryConfig cfg)
    {
        if (!m_Loaded || !flag || !mgr || !cfg || groupID == "")
            return;

        vector center = flag.GetPosition();
        float radius = cfg.m_BuildRadiusMeters;
        float enterSq = radius * radius;
        float keepRadius = radius + EXIT_MARGIN;
        float keepSq = keepRadius * keepRadius;

        m_ScanObjects.Clear();
        m_ScanCargo.Clear();
        m_Candidates.Clear();
        m_CandidateKeys.Clear();
        GetGame().GetObjectsAtPosition(center, keepRadius, m_ScanObjects, m_ScanCargo);

        int objCount = m_ScanObjects.Count();
        int oi;
        for (oi = 0; oi < objCount; oi = oi + 1)
        {
            Transport found = Transport.Cast(m_ScanObjects[oi]);
            if (!found)
                continue;
            if (found.IsRuined())
                continue;
            if (DistanceSqXZ(found.GetPosition(), center) > keepSq)
                continue;
            string foundKey = KeyOf(found);
            if (foundKey == "")
                continue;
            if (m_Candidates.Contains(foundKey))
                continue;
            m_Candidates.Set(foundKey, found);
            m_CandidateKeys.Insert(foundKey);
        }

        // From the back, so an index is still the position before this scan.
        array<string> scanQueue = null;
        if (m_Queues.Find(groupID, scanQueue) && scanQueue)
        {
            int qi;
            for (qi = scanQueue.Count() - 1; qi >= 0; qi = qi - 1)
            {
                string queuedKey = scanQueue[qi];
                Transport seen = null;
                if (m_Candidates.Find(queuedKey, seen) && seen)
                {
                    // An id carried by two live entities identifies neither: the
                    // entity already bound keeps the key.
                    Transport current = null;
                    m_Bound.Find(queuedKey, current);
                    if (!current || current == seen)
                    {
                        if (!current)
                            LogFound(groupID, queuedKey, seen, qi);
                        Bind(queuedKey, seen);
                    }
                    continue;
                }
                // A live bound vehicle is judged by its position in ApplyProtection.
                Transport alive = null;
                if (m_Bound.Find(queuedKey, alive) && alive)
                    continue;
                int misses = 0;
                m_Misses.Find(queuedKey, misses);
                misses = misses + 1;
                if (misses >= MAX_MISSES)
                    RemoveAt(groupID, scanQueue, qi, "missing", false);
                else
                    m_Misses.Set(queuedKey, misses);
            }
        }

        // Newcomers go to the back, in query order.
        int candCount = m_CandidateKeys.Count();
        int ci;
        for (ci = 0; ci < candCount; ci = ci + 1)
        {
            string newKey = m_CandidateKeys[ci];
            Transport newcomer = null;
            if (!m_Candidates.Find(newKey, newcomer) || !newcomer)
                continue;
            string owner = "";
            if (m_KeyGroup.Find(newKey, owner))
            {
                if (owner == groupID)
                    continue;
                // Another live entity bound to this id keeps it where it is.
                Transport ownerBound = null;
                if (m_Bound.Find(newKey, ownerBound) && ownerBound && ownerBound != newcomer)
                    continue;
                // Another base keeps the vehicle until it is beyond that base's exit distance.
                bool ownerActive = false;
                if (!HasLeftZone(owner, newcomer, mgr, cfg, ownerActive))
                    continue;
                RemoveKey(owner, newKey, "moved", ownerActive);
            }
            if (DistanceSqXZ(newcomer.GetPosition(), center) > enterSq)
                continue;
            Append(groupID, newKey, newcomer);
        }
    }

    // ownerActive: that base's flag still protects (registered and raised).
    protected bool HasLeftZone(string groupID, Transport vehicle, LFPG_GroupManager mgr, LFPG_TerritoryConfig cfg, out bool ownerActive)
    {
        ownerActive = false;
        LFPG_FlagBase zoneFlag = GetRegisteredFlag(groupID, mgr);
        if (!zoneFlag)
            return true;
        ownerActive = IsRaised(zoneFlag);
        float zoneKeep = cfg.m_BuildRadiusMeters + EXIT_MARGIN;
        float zoneKeepSq = zoneKeep * zoneKeep;
        return DistanceSqXZ(vehicle.GetPosition(), zoneFlag.GetPosition()) > zoneKeepSq;
    }

    // The flag registered to a live group, or null.
    protected LFPG_FlagBase GetRegisteredFlag(string groupID, LFPG_GroupManager mgr)
    {
        LFPG_FlagBase regFlag = mgr.GetGroupFlag(groupID);
        if (!regFlag || !mgr.IsOwnedRegisteredFlag(regFlag))
            return null;
        return regFlag;
    }

    protected static bool IsRaised(LFPG_FlagBase flag)
    {
        if (!flag)
            return false;
        return flag.ComputeCurrentRaiseProgress() > 0.0;
    }

    // ========================================================================
    // PROTECTION - every validation tick, for every queue
    // ========================================================================

    // Pass 1, every queue: drop ruined, re-keyed and departed vehicles.
    // Pass 2, every queue: the first bound vehicles of each raised registered flag
    // hold its slots. Every icon of the tick is cleared before any is set, so a
    // vehicle never ends a tick unmarked while it holds a slot.
    void ApplyProtection(LFPG_GroupManager mgr, LFPG_TerritoryConfig cfg)
    {
        if (!m_Loaded || !mgr || !cfg)
            return;

        float keepRadius = cfg.m_BuildRadiusMeters + EXIT_MARGIN;
        float keepSq = keepRadius * keepRadius;
        float wanted = cfg.m_VehicleLifetime;
        // Two ticks of decay plus a margin. Lower means something else shortened it.
        float floorLimit = wanted - (2.0 * cfg.m_ValidationTickSeconds) - 5.0;

        CollectGroupIDs();
        int groupCount = m_GroupBuffer.Count();
        int gi;
        for (gi = 0; gi < groupCount; gi = gi + 1)
        {
            string exitGroup = m_GroupBuffer[gi];
            array<string> exitQueue = null;
            if (!m_Queues.Find(exitGroup, exitQueue) || !exitQueue)
                continue;

            // Read before any removal: the cut of D5 needs a flag that still protects
            // and a zone that did not move since the last pass.
            LFPG_FlagBase exitFlag = GetRegisteredFlag(exitGroup, mgr);
            bool exitActive = IsRaised(exitFlag);
            vector center = "0 0 0";
            if (exitFlag)
            {
                center = exitFlag.GetPosition();
                vector lastCenter = "0 0 0";
                if (m_LastCenter.Find(exitGroup, lastCenter) && DistanceSqXZ(lastCenter, center) > EXIT_MARGIN * EXIT_MARGIN)
                {
                    exitActive = false;
                    string movedMsg = "Flag of group ";
                    movedMsg = movedMsg + exitGroup;
                    movedMsg = movedMsg + " moved from " + lastCenter.ToString();
                    movedMsg = movedMsg + " to " + center.ToString();
                    movedMsg = movedMsg + "; vehicles left outside its zone keep their lifetime.";
                    LFPG_Log.Info(movedMsg);
                }
                m_LastCenter.Set(exitGroup, center);
            }

            int xi;
            for (xi = exitQueue.Count() - 1; xi >= 0; xi = xi - 1)
            {
                string exitKey = exitQueue[xi];
                Transport exitVehicle = null;
                if (!m_Bound.Find(exitKey, exitVehicle) || !exitVehicle)
                    continue;
                // An empty id proves nothing; only a different id moves the key.
                string liveKey = KeyOf(exitVehicle);
                if (exitVehicle.IsRuined())
                    RemoveAt(exitGroup, exitQueue, xi, "ruined", false);
                else if (liveKey != "" && liveKey != exitKey)
                    RemoveAt(exitGroup, exitQueue, xi, "rekeyed", false);
                else if (exitFlag && DistanceSqXZ(exitVehicle.GetPosition(), center) > keepSq)
                    RemoveAt(exitGroup, exitQueue, xi, "left", exitActive);
            }
        }

        m_SlotKeys.Clear();
        m_SlotGroups.Clear();
        for (gi = 0; gi < groupCount; gi = gi + 1)
        {
            string slotGroup = m_GroupBuffer[gi];
            array<string> slotQueue = null;
            if (!m_Queues.Find(slotGroup, slotQueue) || !slotQueue)
                continue;

            LFPG_FlagBase slotFlag = GetRegisteredFlag(slotGroup, mgr);
            int limit = 0;
            if (IsRaised(slotFlag))
                limit = cfg.GetVehiclesProtected(slotFlag.GetTier());

            int used = 0;
            int pi;
            for (pi = 0; pi < slotQueue.Count(); pi = pi + 1)
            {
                string slotKey = slotQueue[pi];
                Transport slotVehicle = null;
                if (!m_Bound.Find(slotKey, slotVehicle) || !slotVehicle)
                    continue;
                if (used < limit && !slotVehicle.IsRuined())
                {
                    m_SlotKeys.Insert(slotKey);
                    m_SlotGroups.Insert(slotGroup);
                    used = used + 1;
                }
                else
                {
                    Unprotect(slotGroup, slotKey, slotVehicle);
                }
            }
        }

        int slotCount = m_SlotKeys.Count();
        int si;
        for (si = 0; si < slotCount; si = si + 1)
        {
            string heldKey = m_SlotKeys[si];
            string heldGroup = m_SlotGroups[si];
            Transport heldVehicle = null;
            if (m_Bound.Find(heldKey, heldVehicle) && heldVehicle)
                Protect(heldGroup, heldKey, heldVehicle, wanted, floorLimit);
        }

        CompactEmptyQueues();
    }

    // Raises the remaining lifetime to the floor; never lowers it (D9).
    protected void Protect(string groupID, string key, Transport vehicle, float wanted, float floorLimit)
    {
        float before = vehicle.GetLifetime();
        bool wasProtected = vehicle.LFPG_IsProtected();
        if (wasProtected && before < floorLimit && !m_FloorLogged.Contains(key))
        {
            m_FloorLogged.Set(key, true);
            string floorMsg = "Vehicle lifetime below the protected floor: ";
            floorMsg = floorMsg + key;
            floorMsg = floorMsg + " group=" + groupID;
            floorMsg = floorMsg + " lifetime=" + before.ToString();
            floorMsg = floorMsg + " max=" + vehicle.GetLifetimeMax().ToString();
            LFPG_Log.Info(floorMsg);
        }

        if (before < wanted)
            vehicle.SetLifetime(wanted);

        if (wasProtected)
            return;

        vehicle.LFPG_SetProtected(true);
        string protMsg = "Vehicle protected: ";
        protMsg = protMsg + key;
        protMsg = protMsg + " group=" + groupID;
        protMsg = protMsg + " before=" + before.ToString();
        protMsg = protMsg + " after=" + vehicle.GetLifetime().ToString();
        protMsg = protMsg + " max=" + vehicle.GetLifetimeMax().ToString();
        LFPG_Log.Info(protMsg);
    }

    // Loses the slot without moving: the lifetime is kept (D5).
    protected void Unprotect(string groupID, string key, Transport vehicle)
    {
        m_FloorLogged.Remove(key);
        if (!vehicle.LFPG_IsProtected())
            return;

        vehicle.LFPG_SetProtected(false);
        string unprotMsg = "Vehicle unprotected: ";
        unprotMsg = unprotMsg + key;
        unprotMsg = unprotMsg + " group=" + groupID;
        unprotMsg = unprotMsg + " lifetime=" + vehicle.GetLifetime().ToString();
        unprotMsg = unprotMsg + " max=" + vehicle.GetLifetimeMax().ToString();
        LFPG_Log.Info(unprotMsg);
    }

    // Clears every icon without touching lifetimes. Used when the group state
    // stops being trusted during the session.
    void UnprotectAll()
    {
        if (!m_Loaded)
            return;
        foreach (string boundKey, Transport boundVehicle : m_Bound)
        {
            if (boundVehicle && boundVehicle.LFPG_IsProtected())
                boundVehicle.LFPG_SetProtected(false);
        }
        m_FloorLogged.Clear();
    }

    // ========================================================================
    // QUEUE EDITS
    // ========================================================================

    // First time this session a queued key is tied to a live vehicle.
    protected void LogFound(string groupID, string key, Transport vehicle, int index)
    {
        int place = index + 1;
        string foundMsg = "Vehicle found: ";
        foundMsg = foundMsg + key;
        foundMsg = foundMsg + " group=" + groupID;
        foundMsg = foundMsg + " place=" + place.ToString();
        foundMsg = foundMsg + " type=" + vehicle.GetType();
        foundMsg = foundMsg + " pos=" + vehicle.GetPosition().ToString();
        LFPG_Log.Info(foundMsg);
    }

    // A key bound to another live entity until now leaves that entity unmarked.
    protected void Bind(string key, Transport vehicle)
    {
        Transport previous = null;
        if (m_Bound.Find(key, previous) && previous && previous != vehicle)
            previous.LFPG_SetProtected(false);
        m_Bound.Set(key, vehicle);
        m_Misses.Remove(key);
    }

    protected void Append(string groupID, string key, Transport vehicle)
    {
        array<string> appendQueue = null;
        if (!m_Queues.Find(groupID, appendQueue) || !appendQueue)
        {
            appendQueue = new array<string>;
            m_Queues.Set(groupID, appendQueue);
        }
        appendQueue.Insert(key);
        m_KeyGroup.Set(key, groupID);
        Bind(key, vehicle);
        m_Dirty = true;

        string addMsg = "Vehicle queued: ";
        addMsg = addMsg + key;
        addMsg = addMsg + " group=" + groupID;
        addMsg = addMsg + " place=" + appendQueue.Count().ToString();
        addMsg = addMsg + " type=" + vehicle.GetType();
        addMsg = addMsg + " pos=" + vehicle.GetPosition().ToString();
        LFPG_Log.Info(addMsg);
    }

    // Removes queue[index] and clears its icon. With clip, a vehicle that held a
    // protected slot leaves with at most its own maximum lifetime (D5).
    protected void RemoveAt(string groupID, array<string> queue, int index, string reason, bool clip)
    {
        string key = queue[index];
        Transport vehicle = null;
        m_Bound.Find(key, vehicle);
        string clipDetail = "";
        if (vehicle)
        {
            if (clip && vehicle.LFPG_IsProtected())
            {
                float lifeNow = vehicle.GetLifetime();
                float lifeMax = vehicle.GetLifetimeMax();
                if (lifeNow > lifeMax)
                {
                    vehicle.SetLifetime(lifeMax);
                    clipDetail = " clipped=" + lifeNow.ToString();
                    clipDetail = clipDetail + "->" + vehicle.GetLifetime().ToString();
                    clipDetail = clipDetail + " max=" + lifeMax.ToString();
                }
            }
            vehicle.LFPG_SetProtected(false);
        }

        queue.RemoveOrdered(index);
        m_KeyGroup.Remove(key);
        m_Bound.Remove(key);
        m_Misses.Remove(key);
        m_FloorLogged.Remove(key);
        m_Dirty = true;

        string removeMsg = "Vehicle dequeued: ";
        removeMsg = removeMsg + key;
        removeMsg = removeMsg + " group=" + groupID;
        removeMsg = removeMsg + " reason=" + reason;
        removeMsg = removeMsg + clipDetail;
        LFPG_Log.Info(removeMsg);
    }

    protected void RemoveKey(string groupID, string key, string reason, bool clip)
    {
        array<string> ownerQueue = null;
        if (!m_Queues.Find(groupID, ownerQueue) || !ownerQueue)
            return;
        int keyIndex = ownerQueue.Find(key);
        if (keyIndex < 0)
            return;
        RemoveAt(groupID, ownerQueue, keyIndex, reason, clip);
    }

    // Dissolved or missing group: the queue goes, icons go off, lifetimes stay (R10).
    void DropGroup(string groupID, string reason)
    {
        array<string> dropQueue = null;
        if (!m_Queues.Find(groupID, dropQueue) || !dropQueue)
            return;

        int dropCount = dropQueue.Count();
        int di;
        for (di = 0; di < dropCount; di = di + 1)
        {
            string dropKey = dropQueue[di];
            Transport dropVehicle = null;
            if (m_Bound.Find(dropKey, dropVehicle) && dropVehicle)
                dropVehicle.LFPG_SetProtected(false);
            m_KeyGroup.Remove(dropKey);
            m_Bound.Remove(dropKey);
            m_Misses.Remove(dropKey);
            m_FloorLogged.Remove(dropKey);
        }
        m_Queues.Remove(groupID);
        m_LastCenter.Remove(groupID);
        m_Dirty = true;

        string dropMsg = "Vehicle queue dropped: group=";
        dropMsg = dropMsg + groupID;
        dropMsg = dropMsg + " vehicles=" + dropCount.ToString();
        dropMsg = dropMsg + " reason=" + reason;
        LFPG_Log.Info(dropMsg);
    }

    // Queues of groups that no longer exist (deleted without DissolveGroup,
    // for example a crash between dissolving and saving this file).
    void PruneMissingGroups(LFPG_GroupManager mgr)
    {
        if (!m_Loaded || !mgr)
            return;
        CollectGroupIDs();
        int pruneCount = m_GroupBuffer.Count();
        int pri;
        for (pri = 0; pri < pruneCount; pri = pri + 1)
        {
            string pruneID = m_GroupBuffer[pri];
            if (!mgr.GroupExists(pruneID))
                DropGroup(pruneID, "group missing");
        }
    }

    protected void CollectGroupIDs()
    {
        m_GroupBuffer.Clear();
        foreach (string queuedGroup, array<string> queuedKeys : m_Queues)
        {
            m_GroupBuffer.Insert(queuedGroup);
        }
    }

    // Empty queues are not saved; drop them at a point where no caller holds one.
    protected void CompactEmptyQueues()
    {
        CollectGroupIDs();
        int compactCount = m_GroupBuffer.Count();
        int cqi;
        for (cqi = 0; cqi < compactCount; cqi = cqi + 1)
        {
            string compactID = m_GroupBuffer[cqi];
            array<string> compactQueue = null;
            if (m_Queues.Find(compactID, compactQueue) && compactQueue && compactQueue.Count() == 0)
            {
                m_Queues.Remove(compactID);
                m_LastCenter.Remove(compactID);
            }
        }
    }

    // ========================================================================
    // SAVE
    // ========================================================================

    bool SaveIfDirty()
    {
        if (!m_Loaded || m_ReadOnly || !m_Dirty)
            return true;
        if (!SaveQueues())
            return false;
        m_Dirty = false;
        m_SaveErrorLogged = false;
        m_CorruptCopied = false;
        return true;
    }

    protected LFPG_VehicleQueuesData BuildData()
    {
        LFPG_VehicleQueuesData data = new LFPG_VehicleQueuesData();
        data.m_Version = FILE_VERSION;
        data.m_Queues = new array<ref LFPG_VehicleQueueRecord>;

        CollectGroupIDs();
        m_GroupBuffer.Sort();
        int buildCount = m_GroupBuffer.Count();
        int bi;
        for (bi = 0; bi < buildCount; bi = bi + 1)
        {
            string buildID = m_GroupBuffer[bi];
            array<string> buildQueue = null;
            if (!m_Queues.Find(buildID, buildQueue) || !buildQueue || buildQueue.Count() == 0)
                continue;
            LFPG_VehicleQueueRecord record = new LFPG_VehicleQueueRecord();
            record.m_GroupID = buildID;
            record.m_Vehicles = new array<string>;
            int buildKeys = buildQueue.Count();
            int bki;
            for (bki = 0; bki < buildKeys; bki = bki + 1)
            {
                string buildKey = buildQueue[bki];
                record.m_Vehicles.Insert(buildKey);
            }
            data.m_Queues.Insert(record);
        }
        return data;
    }

    // 1. The previous state is kept before tmp is touched: a valid final is copied
    //    to the backup, an invalid final is copied aside (once per failing
    //    streak), and without a valid final a valid tmp (left by a crash or used
    //    by a recovery) becomes the backup. Any failure stops the save; the next
    //    tick retries.
    // 2. tmp -> verify -> copy to the final -> verify.
    // 3. tmp is deleted.
    // Not atomic: DayZ has no rename. A valid copy exists at every step.
    protected bool SaveQueues()
    {
        string finalPath = GetFilePath();
        string tmpPath = GetTmpPath();
        string bakPath = GetBackupPath();

        if (CheckFutureFiles())
        {
            m_ReadOnly = true;
            LFPG_Log.Error("Vehicle queue saves stop for this session.");
            return false;
        }

        LFPG_VehicleQueuesData data = BuildData();
        string expected = "";
        string saveError = "";
        if (!JsonFileLoader<LFPG_VehicleQueuesData>.MakeData(data, expected, saveError, false))
            return SaveFailed("cannot serialize the queues");

        // 1. Previous state.
        LFPG_VehicleQueuesData previous;
        string previousError = "";
        if (FileExist(finalPath) && ReadQueuesFile(finalPath, previous, previousError))
        {
            if (!CopyToBackup(finalPath, bakPath))
                return SaveFailed("cannot copy vehicles.json to vehicles.json.bak");
        }
        else
        {
            if (FileExist(finalPath) && !m_CorruptCopied)
            {
                string asidePath = "";
                if (!CopyAside(finalPath, ".corrupt", asidePath))
                    return SaveFailed("the invalid vehicles.json cannot be copied aside");
                m_CorruptCopied = true;
                string asideMsg = "vehicles.json did not verify; copied to ";
                asideMsg = asideMsg + asidePath;
                asideMsg = asideMsg + " before it is replaced.";
                LFPG_Log.Error(asideMsg);
            }
            LFPG_VehicleQueuesData pending;
            string pendingError = "";
            if (FileExist(tmpPath) && ReadQueuesFile(tmpPath, pending, pendingError))
            {
                if (!CopyToBackup(tmpPath, bakPath))
                    return SaveFailed("vehicles.json.tmp holds the last state and cannot be copied to vehicles.json.bak");
            }
        }

        // 2. Write and verify tmp, then promote it. The final is deleted only
        //    when a direct copy fails.
        if (!JsonFileLoader<LFPG_VehicleQueuesData>.SaveFile(tmpPath, data, saveError))
            return SaveFailed("cannot write vehicles.json.tmp: " + saveError);
        if (!FileMatches(tmpPath, expected))
            return SaveFailed("vehicles.json.tmp does not verify");
        if (!CopyFile(tmpPath, finalPath))
        {
            if (FileExist(finalPath) && !DeleteFile(finalPath))
                return SaveFailed("cannot replace vehicles.json");
            if (!CopyFile(tmpPath, finalPath))
                return SaveFailed("cannot copy vehicles.json.tmp to vehicles.json; tmp kept");
        }
        if (!FileMatches(finalPath, expected))
            return SaveFailed("vehicles.json does not verify after the copy; tmp kept");

        // 3. tmp now equals the final. A tmp that cannot be deleted is harmless:
        //    the next boot reads the valid final first.
        if (!DeleteFile(tmpPath) && !m_TmpErrorLogged)
        {
            m_TmpErrorLogged = true;
            LFPG_Log.Error("vehicles.json.tmp could not be deleted; it is a copy of vehicles.json.");
        }
        return true;
    }

    // Replaces the backup with a verified copy of sourcePath.
    protected bool CopyToBackup(string sourcePath, string bakPath)
    {
        if (FileExist(bakPath) && !DeleteFile(bakPath))
            return false;
        if (!CopyFile(sourcePath, bakPath))
            return false;
        return LFPG_GroupsStorage.FilesEqual(sourcePath, bakPath);
    }

    protected bool FileMatches(string path, string expected)
    {
        LFPG_VehicleQueuesData actual;
        string matchError = "";
        if (!ReadQueuesFile(path, actual, matchError))
            return false;
        string actualJSON = "";
        if (!JsonFileLoader<LFPG_VehicleQueuesData>.MakeData(actual, actualJSON, matchError, false))
            return false;
        return actualJSON == expected;
    }

    // Copies a file to path+suffix, or path+suffix.N when that name is taken.
    // A source the text reader cannot read (NUL bytes after a power loss) is
    // checked by its byte count, as GroupManager.MoveAside does.
    protected bool CopyAside(string path, string suffix, out string aside)
    {
        aside = path + suffix;
        int asideIndex = 0;
        while (FileExist(aside))
        {
            asideIndex = asideIndex + 1;
            aside = path + suffix + "." + asideIndex.ToString();
        }
        if (!CopyFile(path, aside))
            return false;
        string sourceText;
        if (LFPG_GroupsStorage.ReadText(path, sourceText))
            return LFPG_GroupsStorage.FilesEqual(path, aside);
        int sourceBytes = LFPG_GroupsStorage.ReadByteCount(path);
        if (sourceBytes < 0)
            return false;
        return LFPG_GroupsStorage.ReadByteCount(aside) == sourceBytes;
    }

    // Logged once per run of failures; the next tick retries.
    protected bool SaveFailed(string reason)
    {
        if (!m_SaveErrorLogged)
        {
            m_SaveErrorLogged = true;
            string failMsg = "vehicles.json save failed: ";
            failMsg = failMsg + reason;
            failMsg = failMsg + ". Retrying each validation tick.";
            LFPG_Log.Error(failMsg);
        }
        return false;
    }
};
