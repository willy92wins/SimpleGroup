// groups.json v2. Logical records retain the v1 DTO and member order.
// Integrity covers the exact UTF-8 bytes of the decoded payload string; see
// GROUPS-FORMAT.md. Adler-32 detects accidental damage, not malicious edits.
class LFPG_GroupsHeader
{
    int m_Version = 0;
};

class LFPG_GroupsEnvelope
{
    int m_Version = 0;
    int m_ExpectedGroups = -1;
    string m_Digest = "";
    int m_PayloadBytes = -1;
    ref array<string> m_PayloadParts;
};

class LFPG_GroupsStorage
{
    static const int FILE_VERSION = 2;
    // Same bound as vanilla JsonFileLoader. A file reaching it is refused.
    static const int MAX_BYTES = 100000000;
    // Native JSON string reads truncate at 1023 bytes. 128 Unicode scalars
    // use at most 512 UTF-8 bytes. Slice by byte offsets: SubstringUtf8 stops
    // working past character offset 8191 in the tested DayZ engine.
    static const int PART_CHARACTERS = 128;

    static string Digest(string payload)
    {
        int a = 1;
        int b = 0;
        int size = payload.Length();
        // Get on a large native string is expensive. Bound the source string
        // for each byte read; preserve Adler state across these raw byte blocks.
        for (int offset = 0; offset < size; offset = offset + 512)
        {
            int length = size - offset;
            if (length > 512)
                length = 512;
            string block = payload.Substring(offset, length);
            for (int i = 0; i < length; i = i + 1)
            {
                int octet = block.Get(i).ToAscii();
                if (octet < 0)
                    octet = octet + 256;
                a = (a + octet) % 65521;
                b = (b + a) % 65521;
            }
        }
        return "adler32:" + b.ToString() + ":" + a.ToString();
    }

    static bool ReadText(string path, out string text)
    {
        text = "";
        FileHandle handle = OpenFile(path, FileMode.READ);
        if (!handle)
            return false;
        int read = ReadFile(handle, text, MAX_BYTES);
        CloseFile(handle);
        return read >= 0 && read < MAX_BYTES && read == text.Length();
    }

    static bool FilesEqual(string first, string second)
    {
        string left;
        string right;
        if (!ReadText(first, left) || !ReadText(second, right))
            return false;
        return left == right;
    }

    static int ReadVersion(string path)
    {
        LFPG_GroupsHeader header = new LFPG_GroupsHeader();
        string raw;
        string error;
        if (!ReadText(path, raw))
            return 0;
        if (!JsonFileLoader<LFPG_GroupsHeader>.LoadData(raw, header, error))
            return 0;
        return header.m_Version;
    }

    static bool LoadFile(string path, out LFPG_GroupsFileData data, out string error)
    {
        data = null;
        string raw;
        if (!ReadText(path, raw))
        {
            error = "Groups file unreadable or outside size bound";
            return false;
        }
        LFPG_GroupsHeader header = new LFPG_GroupsHeader();
        if (!JsonFileLoader<LFPG_GroupsHeader>.LoadData(raw, header, error))
            return false;
        LFPG_GroupsFileData staged = new LFPG_GroupsFileData();
        if (header.m_Version == 1)
        {
            if (!JsonFileLoader<LFPG_GroupsFileData>.LoadData(raw, staged, error))
                return false;
            if ((!staged.m_Groups || staged.m_Groups.Count() == 0) && !HasGroupsKey(raw))
            {
                error = "Missing top-level m_Groups key or failed key scan";
                return false;
            }
        }
        else if (header.m_Version == FILE_VERSION)
        {
            LFPG_GroupsEnvelope envelope = new LFPG_GroupsEnvelope();
            if (!JsonFileLoader<LFPG_GroupsEnvelope>.LoadData(raw, envelope, error))
                return false;
            string payload;
            if (envelope.m_ExpectedGroups < 0 || !ReadPayload(envelope, payload))
            {
                error = "Groups envelope integrity mismatch";
                return false;
            }
            if (!JsonFileLoader<LFPG_GroupsFileData>.LoadData(payload, staged, error))
                return false;
            if ((!staged.m_Groups || staged.m_Groups.Count() == 0) && !HasGroupsKey(payload))
            {
                error = "Missing top-level m_Groups key or failed key scan";
                return false;
            }
            if (!staged.m_Groups || staged.m_Groups.Count() != envelope.m_ExpectedGroups)
            {
                error = "Groups envelope count mismatch";
                return false;
            }
        }
        else
        {
            error = "Unsupported groups file version";
            return false;
        }
        if (!staged || staged.m_Version != 1 || !staged.m_Groups)
        {
            error = "Invalid logical groups payload";
            return false;
        }
        data = staged;
        return true;
    }

    protected static bool HasGroupsKey(string raw)
    {
        array<string> keys;
        if (!LFPG_TerritoryConfig.FindTopLevelKeys(raw, keys))
            return false;
        return keys.Find("m_Groups") >= 0;
    }

    static bool SaveFile(string path, LFPG_GroupsFileData data, out string error)
    {
        if (!data || data.m_Version != 1 || !data.m_Groups)
            return false;
        LFPG_GroupsEnvelope envelope = new LFPG_GroupsEnvelope();
        envelope.m_Version = FILE_VERSION;
        envelope.m_ExpectedGroups = data.m_Groups.Count();
        string payload;
        if (!JsonFileLoader<LFPG_GroupsFileData>.MakeData(data, payload, error, false))
            return false;
        envelope.m_PayloadBytes = payload.Length();
        if (envelope.m_PayloadBytes <= 0 || envelope.m_PayloadBytes >= MAX_BYTES)
            return false;
        envelope.m_PayloadParts = new array<string>;
        int start = 0;
        while (start < envelope.m_PayloadBytes)
        {
            // At most 128 bytes also satisfies the 128-scalar format limit.
            // Inspect only a boundary, not every byte of the large payload.
            int end = start + PART_CHARACTERS;
            if (end >= envelope.m_PayloadBytes)
                end = envelope.m_PayloadBytes;
            else
            {
                while (end > start)
                {
                    int octet = payload.Get(end).ToAscii();
                    if (octet < 0)
                        octet = octet + 256;
                    if (octet < 128 || octet >= 192)
                        break;
                    end = end - 1;
                }
            }
            if (end <= start)
                return false;
            envelope.m_PayloadParts.Insert(payload.Substring(start, end - start));
            start = end;
        }
        envelope.m_Digest = Digest(payload);
        return JsonFileLoader<LFPG_GroupsEnvelope>.SaveFile(path, envelope, error);
    }

    static bool ReadPayload(LFPG_GroupsEnvelope envelope, out string payload)
    {
        payload = "";
        if (!envelope.m_PayloadParts || envelope.m_PayloadParts.Count() == 0)
            return false;
        if (envelope.m_PayloadBytes <= 0 || envelope.m_PayloadBytes >= MAX_BYTES)
            return false;
        int bytes = 0;
        for (int i = 0; i < envelope.m_PayloadParts.Count(); i = i + 1)
        {
            string part = envelope.m_PayloadParts[i];
            if (part == "" || part.LengthUtf8() > PART_CHARACTERS || part.Length() > 512)
                return false;
            bytes = bytes + part.Length();
            if (bytes > envelope.m_PayloadBytes)
                return false;
            payload = payload + part;
        }
        return bytes == envelope.m_PayloadBytes && Digest(payload) == envelope.m_Digest;
    }

    // Never overwrite an earlier migration backup. A retried migration can reuse
    // its byte-identical copy; migration after rollback gets a numbered copy.
    static bool PreserveLegacy(string path, string backupBase)
    {
        if (!FileExist(path))
            return true;
        int version = ReadVersion(path);
        if (version == FILE_VERSION)
            return true;
        if (version != 1)
            return false;
        string backup = backupBase;
        int index = 0;
        while (FileExist(backup))
        {
            if (FilesEqual(path, backup))
                return true;
            index = index + 1;
            backup = backupBase + "." + index.ToString();
        }
        if (!CopyFile(path, backup))
            return false;
        return FilesEqual(path, backup);
    }
};
