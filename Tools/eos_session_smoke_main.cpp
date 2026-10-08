#include "../Source/Citix/Network/CitixEOSDeviceProofCore.h"
#include "eos_sessions.h"
#include <iostream>
#include <stdexcept>

// Context outlives platform release, including callbacks pending at timeout.
struct Probe
{
    bool Done = false;
    EOS_EResult Code = EOS_EResult::EOS_UnexpectedError;
    std::string SessionId, Error, Stage = "Login";
    std::map<std::string, std::string> Steps;
    bool Match = false, Created = false, Cleaned = false;
    bool Registered = false;
    template<class T> static void EOS_CALL Completed(const T* Data)
    {
        auto& Self = *static_cast<Probe*>(Data->ClientData);
        Self.Code = Data->ResultCode; Self.Done = true;
    }
    static void EOS_CALL Published(const EOS_Sessions_UpdateSessionCallbackInfo* Data)
    {
        auto& Self = *static_cast<Probe*>(Data->ClientData);
        if (Data->SessionId) Self.SessionId = Data->SessionId;
        Self.Created = Data->ResultCode == EOS_EResult::EOS_Success;
        Completed(Data);
    }
    void Check(const char* Name, EOS_EResult Result)
    {
        Stage = Name; Steps[Name] = EOS_EResult_ToString(Result);
        std::cout << Name << ": " << Steps[Name] << std::endl;
        if (Result != EOS_EResult::EOS_Success) throw std::runtime_error(Name);
    }
    void Wait(EOS_HPlatform Platform, const char* Name)
    {
        const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!Done && std::chrono::steady_clock::now() < End)
        {
            EOS_Platform_Tick(Platform);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(Name, Done ? Code : EOS_EResult::EOS_TimedOut);
    }
    bool Run(EOS_HPlatform Platform, EOS_ProductUserId User)
    {
        const auto Sessions = EOS_Platform_GetSessionsInterface(Platform);
        EOS_HSessionModification Modification = nullptr;
        EOS_HSessionSearch Search = nullptr;
        EOS_HSessionDetails Details = nullptr;
        EOS_SessionDetails_Info* Info = nullptr;
        const char* Name = "CitixGuestBackendProof";
        char Id[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {}; int32_t Length = sizeof(Id);
        EOS_ProductUserId_ToString(User, Id, &Length);
        const std::string Address = std::string("EOS:") + Id + ":CitixProof:0";
        try
        {
            EOS_Sessions_CreateSessionModificationOptions Create = {};
            Create.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
            Create.SessionName = Name; Create.BucketId = "CitixChase-BackendProof-v1";
            Create.MaxPlayers = 2; Create.LocalUserId = User;
            Check("BuildSession", EOS_Sessions_CreateSessionModification(Sessions, &Create, &Modification));
            EOS_SessionModification_SetHostAddressOptions Host = {EOS_SESSIONMODIFICATION_SETHOSTADDRESS_API_LATEST, Address.c_str()};
            Check("SetAddress", EOS_SessionModification_SetHostAddress(Modification, &Host));
            EOS_SessionModification_SetPermissionLevelOptions Permission = {EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST, EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised};
            Check("SetPublic", EOS_SessionModification_SetPermissionLevel(Modification, &Permission));
            EOS_Sessions_UpdateSessionOptions Update = {EOS_SESSIONS_UPDATESESSION_API_LATEST, Modification};
            Done = false; EOS_Sessions_UpdateSession(Sessions, &Update, this, Published); Wait(Platform, "Publish");
            EOS_Sessions_RegisterPlayersOptions Register = {EOS_SESSIONS_REGISTERPLAYERS_API_LATEST, Name, &User, 1};
            Done = false; EOS_Sessions_RegisterPlayers(Sessions, &Register, this, Completed<EOS_Sessions_RegisterPlayersCallbackInfo>);
            try { Wait(Platform, "RegisterGuest"); Registered = true; }
            catch (...) { if (Code != EOS_EResult::EOS_ClientPolicyMissingAction || !Done) throw; Error = "RegisterGuest: " + Steps["RegisterGuest"]; }
            EOS_Sessions_CreateSessionSearchOptions NewSearch = {EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST, 1};
            Check("CreateSearch", EOS_Sessions_CreateSessionSearch(Sessions, &NewSearch, &Search));
            EOS_SessionSearch_SetSessionIdOptions Filter = {EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST, SessionId.c_str()};
            Check("FilterSession", EOS_SessionSearch_SetSessionId(Search, &Filter));
            EOS_SessionSearch_FindOptions Find = {EOS_SESSIONSEARCH_FIND_API_LATEST, User};
            Done = false; EOS_SessionSearch_Find(Search, &Find, this, Completed<EOS_SessionSearch_FindCallbackInfo>); Wait(Platform, "Discover");
            EOS_SessionSearch_CopySearchResultByIndexOptions Copy = {EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST, 0};
            Check("CopyResult", EOS_SessionSearch_CopySearchResultByIndex(Search, &Copy, &Details));
            EOS_SessionDetails_CopyInfoOptions Get = {EOS_SESSIONDETAILS_COPYINFO_API_LATEST};
            Check("CopyInfo", EOS_SessionDetails_CopyInfo(Details, &Get, &Info));
            Match = Info->SessionId && SessionId == Info->SessionId && Info->HostAddress && Address == Info->HostAddress && Info->NumOpenPublicConnections == (Registered ? 1u : 2u) && Info->Settings && Info->Settings->NumPublicConnections == 2;
            if (!Match) { Stage = "VerifyDiscoveredSession"; throw std::runtime_error("Session metadata/capacity did not round-trip"); }
        }
        catch (const std::exception& Failure) { if (!Error.empty()) Error += "; "; Error += std::string(Failure.what()) + ": " + Steps[Stage]; }
        const auto FailureStage = !Registered && Steps.count("RegisterGuest") ? "RegisterGuest" : Stage;
        if (Info) EOS_SessionDetails_Info_Release(Info);
        if (Details) EOS_SessionDetails_Release(Details);
        if (Search) EOS_SessionSearch_Release(Search);
        if (Modification) EOS_SessionModification_Release(Modification);
        if (Created)
        {
            EOS_Sessions_DestroySessionOptions Destroy = {EOS_SESSIONS_DESTROYSESSION_API_LATEST, Name};
            Done = false; EOS_Sessions_DestroySession(Sessions, &Destroy, this, Completed<EOS_Sessions_DestroySessionCallbackInfo>);
            try { Wait(Platform, "Destroy"); Cleaned = true; }
            catch (...) { if (Error.empty()) Error = "Session cleanup failed"; }
        }
        Stage = Error.empty() ? "Complete" : FailureStage;
        return Error.empty() && Match && Cleaned;
    }
};

int main(int Count, char** Args)
{
    if (Count != 4) { std::cerr << "Usage: CitixEOSSessionSmoke config.ini new-report.json device-tag\n"; return 2; }
    const std::string Report = Args[2];
    const auto DetailPath = CitixEOSProof::Utf8Path(Report + ".sessions.json");
    if (std::filesystem::exists(DetailPath) || std::filesystem::exists(CitixEOSProof::Utf8Path(Report)))
    { std::cerr << "Choose a new report path; existing evidence is preserved.\n"; return 3; }
    Probe Test;
    const int Code = CitixEOSProof::Run(Args[1], Report, Args[3], "UEBundledSDKSessionSmoke", "5.8.3",
        [&](EOS_HPlatform Platform, EOS_ProductUserId User) { return Test.Run(Platform, User); });
    std::ofstream Out(DetailPath);
    Out << "{\n\"schema\":1,\n\"success\":" << (Code == 0 ? "true" : "false")
        << ",\n\"stage\":" << CitixEOSProof::Quote(Test.Stage)
        << ",\n\"error\":" << CitixEOSProof::Quote(Test.Error)
        << ",\n\"session_id\":" << CitixEOSProof::Quote(Test.SessionId)
        << ",\n\"metadata_verified\":" << (Test.Match ? "true" : "false")
        << ",\n\"cleanup_confirmed\":" << (Test.Cleaned ? "true" : "false");
    for (const auto& Step : Test.Steps) Out << ",\n" << CitixEOSProof::Quote(Step.first) << ':' << CitixEOSProof::Quote(Step.second);
    Out << "\n}\n"; Out.flush();
    return Out ? Code : 3;
}
