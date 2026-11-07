#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include "BundleFormat.h"

namespace fs = std::filesystem;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    wchar_t selfPath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, selfPath, MAX_PATH)) {
        MessageBoxW(NULL, L"Failed to determine executable path.", L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::ifstream selfFile(selfPath, std::ios::binary | std::ios::ate);
    if (!selfFile.is_open()) {
        MessageBoxW(NULL, L"Failed to open executable for reading.", L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    uint64_t totalSize = selfFile.tellg();
    if (totalSize < sizeof(BundleTrailer)) {
        MessageBoxW(NULL, L"Executable is missing bundled game payload.", L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Read trailer (last 24 bytes)
    selfFile.seekg(totalSize - sizeof(BundleTrailer));
    BundleTrailer trailer;
    selfFile.read(reinterpret_cast<char*>(&trailer), sizeof(trailer));

    if (memcmp(trailer.magic, "AGYPACK1", 8) != 0) {
        MessageBoxW(NULL,
            L"This is an unbundled Tessera Bootstrapper stub.\n\n"
            L"To create a standalone game, use the Tessera Editor:\n"
            L"File -> Export Standalone Game (Ctrl+B) with 'Embed all files' checked.",
            L"Tessera Standalone Game Runner", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    // Verify offset and size sanity
    if (trailer.payloadOffset + trailer.payloadSize + sizeof(BundleTrailer) != totalSize) {
        MessageBoxW(NULL, L"Corrupted single-file game bundle.", L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Read header at payloadOffset
    selfFile.seekg(trailer.payloadOffset);
    BundleHeader header;
    selfFile.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (memcmp(header.magic, "AGYPACK1", 8) != 0) {
        MessageBoxW(NULL, L"Corrupted bundle header.", L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Derive cache folder in %LOCALAPPDATA%/TesseraGames/<Title>_<hash>/
    fs::path baseCacheDir;
    const wchar_t* envLocal = _wgetenv(L"LOCALAPPDATA");
    if (envLocal && wcslen(envLocal) > 0) {
        baseCacheDir = fs::path(envLocal) / L"TesseraGames";
    } else {
        wchar_t tempBuf[MAX_PATH];
        GetTempPathW(MAX_PATH, tempBuf);
        baseCacheDir = fs::path(tempBuf) / L"TesseraGames";
    }

    fs::path exeFsPath(selfPath);
    std::wstring exeStem = exeFsPath.stem().wstring();

    std::wstringstream hashStream;
    hashStream << std::hex << trailer.payloadOffset << L"_" << trailer.payloadSize;
    fs::path appCacheDir = baseCacheDir / (exeStem + L"_" + hashStream.str());

    fs::path markerPath = appCacheDir / L".agy_ready";
    bool needExtract = true;
    if (fs::exists(markerPath)) {
        needExtract = false;
    }

    if (needExtract) {
        std::error_code ec;
        fs::create_directories(appCacheDir, ec);

        // Extract files
        for (uint32_t i = 0; i < header.fileCount; ++i) {
            uint32_t pathLen = 0;
            selfFile.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
            if (!selfFile || pathLen == 0 || pathLen > 4096) {
                MessageBoxW(NULL, L"Failed reading bundle file entry.", L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
                return 1;
            }

            std::string relPath(pathLen, '\0');
            selfFile.read(&relPath[0], pathLen);

            uint64_t fileSize = 0;
            selfFile.read(reinterpret_cast<char*>(&fileSize), sizeof(fileSize));

            fs::path outFilePath = appCacheDir / fs::u8path(relPath);
            fs::create_directories(outFilePath.parent_path(), ec);

            std::ofstream outFile(outFilePath, std::ios::binary);
            if (!outFile.is_open()) {
                std::wstring errMsg = L"Failed to write extracted file: " + outFilePath.wstring();
                MessageBoxW(NULL, errMsg.c_str(), L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
                return 1;
            }

            const size_t CHUNK_SIZE = 64 * 1024;
            std::vector<char> buffer(CHUNK_SIZE);
            uint64_t bytesLeft = fileSize;
            while (bytesLeft > 0) {
                size_t toRead = static_cast<size_t>((std::min)(bytesLeft, static_cast<uint64_t>(CHUNK_SIZE)));
                selfFile.read(buffer.data(), toRead);
                outFile.write(buffer.data(), toRead);
                bytesLeft -= toRead;
            }
            outFile.close();
        }

        // Write completion marker
        std::ofstream marker(markerPath);
        marker << "READY";
        marker.close();
    }

    selfFile.close();

    // Determine executable to launch
    std::string mainExeStr(header.mainExeName);
    if (mainExeStr.empty()) mainExeStr = "GameRunner.exe";
    fs::path targetExe = appCacheDir / fs::u8path(mainExeStr);

    if (!fs::exists(targetExe)) {
        std::wstring errMsg = L"Could not find game executable in bundle: " + targetExe.wstring();
        MessageBoxW(NULL, errMsg.c_str(), L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Launch game process
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    LPWSTR cmdLine = GetCommandLineW();
    std::wstring workingDir = appCacheDir.wstring();
    std::wstring targetExeStr = targetExe.wstring();

    BOOL success = CreateProcessW(
        targetExeStr.c_str(),
        cmdLine,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        workingDir.c_str(),
        &si,
        &pi
    );

    if (!success) {
        std::wstring errMsg = L"Failed to launch game process (Error code " + std::to_wstring(GetLastError()) + L").";
        MessageBoxW(NULL, errMsg.c_str(), L"Tessera Launcher Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Wait for the game to finish
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}

