#pragma once

// The same Connect sequence runs in the UE commandlet and portable Windows probe.
// This proof intentionally does not register a user with OSS or create sessions.
#include "eos_sdk.h"
#include "eos_version.h"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <functional>

namespace CitixEOSProof
{
inline std::filesystem::path Utf8Path(const std::string& Value)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(Value.data()), Value.size()));
#else
    return std::filesystem::u8path(Value);
#endif
}

inline std::string Trim(const std::string& Value)
{
    const auto First = Value.find_first_not_of(" \t\r\n");
    if (First == std::string::npos) return {};
    return Value.substr(First, Value.find_last_not_of(" \t\r\n") - First + 1);
}

inline std::string Quote(const std::string& Value)
{
    std::string Out = "\"";
    for (const unsigned char C : Value)
    {
        if (C == '"' || C == '\\') { Out += '\\'; Out += char(C); }
        else if (C < 32) Out += ' '; // Config IDs/stage names are printable ASCII.
        else Out += char(C);
    }
    return Out + '"';
}

struct Result
{
    bool Success = false;
    std::string Stage = "Config";
    std::string Error;
    std::string Puid;
    std::string DeviceResult;
    std::string LoginResult;
    std::string CreateUserResult;
};

struct State
{
    EOS_HConnect Connect = nullptr;
    Result Report;
    bool Done = false;

    void Fail(const char* Stage, EOS_EResult Code)
    {
        Report.Stage = Stage;
        Report.Error = EOS_EResult_ToString(Code);
        Done = true;
    }

    void Finish(EOS_ProductUserId Id)
    {
        char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
        int32_t Length = sizeof(Buffer);
        if (!EOS_ProductUserId_IsValid(Id) ||
            EOS_Connect_GetLoginStatus(Connect, Id) != EOS_ELoginStatus::EOS_LS_LoggedIn)
        {
            Fail("VerifyLoginStatus", EOS_EResult::EOS_InvalidUser);
            return;
        }
        const auto Code = EOS_ProductUserId_ToString(Id, Buffer, &Length);
        if (Code != EOS_EResult::EOS_Success) { Fail("SerializePUID", Code); return; }
        Report.Puid = Buffer;
        Report.Success = true;
        Report.Stage = "Complete";
        Done = true;
    }

    static void EOS_CALL UserCreated(const EOS_Connect_CreateUserCallbackInfo* Data)
    {
        auto& Self = *static_cast<State*>(Data->ClientData);
        Self.Report.CreateUserResult = EOS_EResult_ToString(Data->ResultCode);
        if (Data->ResultCode == EOS_EResult::EOS_Success) Self.Finish(Data->LocalUserId);
        else Self.Fail("CreateUser", Data->ResultCode);
    }

    static void EOS_CALL LoggedIn(const EOS_Connect_LoginCallbackInfo* Data)
    {
        auto& Self = *static_cast<State*>(Data->ClientData);
        Self.Report.LoginResult = EOS_EResult_ToString(Data->ResultCode);
        if (Data->ResultCode == EOS_EResult::EOS_Success) Self.Finish(Data->LocalUserId);
        else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser && Data->ContinuanceToken)
        {
            Self.Report.Stage = "CreateUser";
            EOS_Connect_CreateUserOptions Options = {};
            Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
            Options.ContinuanceToken = Data->ContinuanceToken;
            EOS_Connect_CreateUser(Self.Connect, &Options, &Self, UserCreated);
        }
        else Self.Fail("ConnectLogin", Data->ResultCode);
    }

    static void EOS_CALL DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
    {
        auto& Self = *static_cast<State*>(Data->ClientData);
        Self.Report.DeviceResult = EOS_EResult_ToString(Data->ResultCode);
        if (Data->ResultCode != EOS_EResult::EOS_Success &&
            Data->ResultCode != EOS_EResult::EOS_DuplicateNotAllowed)
        {
            Self.Fail("CreateDeviceId", Data->ResultCode);
            return;
        }
        Self.Report.Stage = "ConnectLogin";
        EOS_Connect_Credentials Credentials = {};
        Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
        Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
        Credentials.Token = nullptr;
        EOS_Connect_UserLoginInfo UserInfo = {};
        UserInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
        UserInfo.DisplayName = "CitixDriver";
        EOS_Connect_LoginOptions Options = {};
        Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
        Options.Credentials = &Credentials;
        Options.UserLoginInfo = &UserInfo; // Required for Device ID even on desktop.
        EOS_Connect_Login(Self.Connect, &Options, &Self, LoggedIn);
    }
};

inline int Run(const std::string& ConfigPath, const std::string& OutputPath,
               const std::string& DeviceTag, const std::string& RuntimeKind,
               const std::string& EngineVersion,
               const std::function<bool(EOS_HPlatform, EOS_ProductUserId)>& AfterLogin = {})
{
    State Self;
    std::error_code PathError;
    // Direct commandlet/CLI use must also preserve an existing config/report.
    if (OutputPath.empty() || std::filesystem::exists(Utf8Path(OutputPath), PathError) || PathError)
        return 3;
    std::map<std::string, std::string> Config;
    std::ifstream File(Utf8Path(ConfigPath));
    bool InSection = false;
    std::string Line;
    while (std::getline(File, Line))
    {
        Line = Trim(Line);
        if (Line.empty() || Line[0] == ';' || Line[0] == '#') continue;
        if (Line[0] == '[') { InSection = Line == "[CitixEOSProof]"; continue; }
        const auto Equal = Line.find('=');
        if (InSection && Equal != std::string::npos)
            Config[Trim(Line.substr(0, Equal))] = Trim(Line.substr(Equal + 1));
    }
    for (const auto* Key : {"ProductId", "SandboxId", "DeploymentId", "ClientId", "ClientSecret"})
        if (Config[Key].empty()) { Self.Report.Error = std::string("Missing ") + Key; break; }
    if (DeviceTag.empty() || OutputPath.empty()) Self.Report.Error = "Device tag and report path required";

    EOS_HPlatform Platform = nullptr;
    bool OwnSDK = false;
    if (Self.Report.Error.empty())
    {
        Self.Report.Stage = "SDKInitialize";
        EOS_InitializeOptions Init = {};
        Init.ApiVersion = EOS_INITIALIZE_API_LATEST;
        Init.ProductName = "CitixChase";
        Init.ProductVersion = "DeviceProof-1";
        const auto Code = EOS_Initialize(&Init);
        OwnSDK = Code == EOS_EResult::EOS_Success;
        if (!OwnSDK && Code != EOS_EResult::EOS_AlreadyConfigured) Self.Fail("SDKInitialize", Code);
        else
        {
            Self.Report.Stage = "PlatformCreate";
            EOS_Platform_Options Options = {};
            Options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
            Options.ProductId = Config["ProductId"].c_str();
            Options.SandboxId = Config["SandboxId"].c_str();
            Options.DeploymentId = Config["DeploymentId"].c_str();
            Options.ClientCredentials.ClientId = Config["ClientId"].c_str();
            Options.ClientCredentials.ClientSecret = Config["ClientSecret"].c_str();
            Options.bIsServer = EOS_FALSE;
            Options.Flags = EOS_PF_DISABLE_OVERLAY | EOS_PF_DISABLE_SOCIAL_OVERLAY;
            // No Auth/EAS, UI, storage encryption, rooms or device-reset calls.
            Platform = EOS_Platform_Create(&Options);
            if (!Platform) Self.Report.Error = "EOS_Platform_Create returned null";
            else
            {
                Self.Connect = EOS_Platform_GetConnectInterface(Platform);
                if (!Self.Connect) Self.Report.Error = "Connect interface unavailable";
                else
                {
                    Self.Report.Stage = "CreateDeviceId";
                    EOS_Connect_CreateDeviceIdOptions Device = {};
                    Device.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
                    Device.DeviceModel = "PC Windows";
                    EOS_Connect_CreateDeviceId(Self.Connect, &Device, &Self, State::DeviceCreated);
                    const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
                    while (!Self.Done && std::chrono::steady_clock::now() < Deadline)
                    {
                        EOS_Platform_Tick(Platform);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    if (!Self.Done) Self.Report.Error = "Timed out after 60 seconds at " + Self.Report.Stage;
                }
            }
        }
    }
    if (Platform && Self.Report.Success && AfterLogin)
    {
        Self.Report.Success = AfterLogin(Platform, EOS_ProductUserId_FromString(Self.Report.Puid.c_str()));
        Self.Report.Stage = Self.Report.Success ? "Complete" : "AfterLoginProbe";
        if (!Self.Report.Success) Self.Report.Error = "Post-login probe failed; see its separate report";
    }
    // Keep callback state alive through release; never tick it after destruction.
    if (Platform) EOS_Platform_Release(Platform);
    if (OwnSDK) EOS_Shutdown();
    std::ofstream Output(Utf8Path(OutputPath), std::ios::trunc);
    if (!Output) return 3;
    Output << "{\n\"schema\":1,\n\"success\":" << (Self.Report.Success ? "true" : "false");
    const std::pair<const char*, std::string> Fields[] = {
        {"auth_method", "DeviceId"}, {"runtime_kind", RuntimeKind}, {"engine_version", EngineVersion},
        {"sdk_version", EOS_GetVersion()}, {"device_tag", DeviceTag},
        {"product_id", Config["ProductId"]}, {"sandbox_id", Config["SandboxId"]},
        {"deployment_id", Config["DeploymentId"]}, {"puid", Self.Report.Puid},
        {"login_status", !Self.Report.Puid.empty() ? "LoggedIn" : "NotLoggedIn"},
        {"stage", Self.Report.Stage}, {"error", Self.Report.Error},
        {"create_device_result", Self.Report.DeviceResult}, {"connect_login_result", Self.Report.LoginResult},
        {"create_user_result", Self.Report.CreateUserResult}
    };
    for (const auto& Field : Fields) Output << ",\n" << Quote(Field.first) << ':' << Quote(Field.second);
    Output << "\n}\n";
    Output.flush();
    if (!Output) return 3;
    return Self.Report.Success ? 0 : 2;
}
}
