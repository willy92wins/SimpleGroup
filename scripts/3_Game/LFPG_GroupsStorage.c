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
    string m_Payload = "";
};

class LFPG_GroupsStorage
{
    static const int FILE_VERSION = 2;
    // Same bound as vanilla JsonFileLoader. A file reaching it is refused.
    static const int MAX_BYTES = 100000000;

    static string Digest(string payload)
    {
        int a = 1;
        int b = 0;
        int size = payload.Length();
        for (int i = 0; i < size; i = i + 1)
        {
            string character = payload.Get(i);
            int octet = character.ToAscii();
            if (octet < 0)
                octet = octet + 256;
            a = (a + octet) % 65521;
            b = (b + a) % 65521;
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
        }
        else if (header.m_Version == FILE_VERSION)
        {
            LFPG_GroupsEnvelope envelope = new LFPG_GroupsEnvelope();
            if (!JsonFileLoader<LFPG_GroupsEnvelope>.LoadData(raw, envelope, error))
                return false;
            if (envelope.m_ExpectedGroups < 0 || envelope.m_Payload == "" || envelope.m_Digest != Digest(envelope.m_Payload))
            {
                error = "Groups envelope integrity mismatch";
                return false;
            }
            if (!JsonFileLoader<LFPG_GroupsFileData>.LoadData(envelope.m_Payload, staged, error))
                return false;
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

    static bool SaveFile(string path, LFPG_GroupsFileData data, out string error)
    {
        if (!data || data.m_Version != 1 || !data.m_Groups)
            return false;
        LFPG_GroupsEnvelope envelope = new LFPG_GroupsEnvelope();
        envelope.m_Version = FILE_VERSION;
        envelope.m_ExpectedGroups = data.m_Groups.Count();
        if (!JsonFileLoader<LFPG_GroupsFileData>.MakeData(data, envelope.m_Payload, error, false))
            return false;
        envelope.m_Digest = Digest(envelope.m_Payload);
        return JsonFileLoader<LFPG_GroupsEnvelope>.SaveFile(path, envelope, error);
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
