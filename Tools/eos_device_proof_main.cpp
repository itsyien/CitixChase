#include "../Source/Citix/Network/CitixEOSDeviceProofCore.h"
#include <iostream>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>

static std::string Utf8(const wchar_t* Value)
{
    const int Count = WideCharToMultiByte(CP_UTF8, 0, Value, -1, nullptr, 0, nullptr, nullptr);
    std::string Result(Count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, Value, -1, &Result[0], Count, nullptr, nullptr);
    Result.pop_back();
    return Result;
}

static std::string DeviceTag()
{
    wchar_t Machine[256] = {};
    DWORD Size = sizeof(Machine);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                    RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, Machine, &Size) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot read Windows machine marker.");
    HANDLE Token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        throw std::runtime_error("Cannot read Windows profile marker.");
    DWORD Needed = 0;
    GetTokenInformation(Token, TokenUser, nullptr, 0, &Needed);
    std::vector<unsigned char> Data(Needed);
    const bool TokenOK = GetTokenInformation(Token, TokenUser, Data.data(), Needed, &Needed) != 0;
    CloseHandle(Token);
    if (!TokenOK) throw std::runtime_error("Cannot read Windows profile marker.");
    wchar_t* Sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(Data.data())->User.Sid, &Sid))
        throw std::runtime_error("Cannot read Windows profile marker.");
    const std::string Marker = Utf8(Machine) + "|" + Utf8(Sid);
    LocalFree(Sid);
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize device marker hash.");
    unsigned char Hash[32] = {};
    const auto Status = BCryptHash(Algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(Marker.data())),
        static_cast<ULONG>(Marker.size()), Hash, sizeof(Hash));
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    if (Status < 0) throw std::runtime_error("Cannot hash device marker.");
    std::string Result;
    for (const auto Byte : Hash)
    {
        Result += "0123456789abcdef"[Byte >> 4];
        Result += "0123456789abcdef"[Byte & 15];
    }
    return Result;
}

int wmain(int Count, wchar_t** Args)
{
    const bool Interactive = Count == 1;
    SetConsoleOutputCP(CP_UTF8);
    std::filesystem::path Folder;
    int Code = 2;
    try
    {
        std::string Config, Report, Device;
        if (Interactive)
        {
            wchar_t Executable[32768] = {};
            if (!GetModuleFileNameW(nullptr, Executable, 32768))
                throw std::runtime_error("Cannot locate the extracted kit.");
            Folder = std::filesystem::path(Executable).parent_path();
            const auto ConfigFile = Folder / L"device-proof.ini";
            if (!std::filesystem::is_regular_file(ConfigFile))
                throw std::runtime_error("Cannot open device-proof.ini. Extract all ZIP files into one folder, then run the executable there.");
            std::filesystem::create_directories(Folder / L"Reports");
            SYSTEMTIME Time;
            GetLocalTime(&Time);
            wchar_t Filename[80];
            swprintf_s(Filename, L"Device-%04u%02u%02u-%02u%02u%02u-%lu.json",
                       Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond,
                       GetCurrentProcessId());
            Config = Utf8(ConfigFile.c_str());
            Report = Utf8((Folder / L"Reports" / Filename).c_str());
            Device = DeviceTag();
            std::cout << "CitixChase - Device ID proof\nConnecting automatically. No Epic login required.\n\n";
        }
        else if (Count == 4)
        {
            Config = Utf8(Args[1]); Report = Utf8(Args[2]); Device = Utf8(Args[3]);
        }
        else throw std::runtime_error("Run without arguments, or supply config path, report path and device tag.");

        Code = CitixEOSProof::Run(Config, Report, Device, "UEBundledSDKPortable", "5.8.3");
        std::cout << (Code == 0 ? "\nSUCCESS: Device ID login succeeded.\n" : "\nFAILED: Device ID proof did not complete.\n")
                  << "Report: " << Report << "\nExit: " << Code << '\n';
        if (Code != 0)
        {
            std::ifstream Result(CitixEOSProof::Utf8Path(Report));
            std::string Line;
            while (std::getline(Result, Line))
                if (Line.rfind("\"error\":", 0) == 0) std::cout << "Reason: " << Line.substr(8) << '\n';
            if (Code == 3) std::cout << "Could not create the report. Extract to a writable folder and retry.\n";
        }
        else std::cout << "Send the JSON file in Reports back for comparison. Do not send manifest.json.\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << "\nFAILED: " << Error.what() << '\n';
        if (!Folder.empty())
        {
            std::ofstream Log(Folder / L"startup-error.txt");
            Log << Error.what() << '\n';
            std::cerr << "Details saved to startup-error.txt when the folder is writable.\n";
        }
    }
    if (Interactive)
    {
        std::cout << "\nPress Enter to close..." << std::flush;
        std::cin.get();
    }
    return Code;
}
