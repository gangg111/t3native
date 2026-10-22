// The builder in one file: this program with the whole builder folder (Python, FFmpeg, the
// converter, the APK template) appended as a packed archive (tools/make_builder.py).
//
// On start it makes sure the archive is unpacked in %LOCALAPPDATA%\Tekken3Builder\app-<id>
// (every file present with its size, else everything unpacked again and checked by CRC-32), then
// runs the window (app\kompilator.pyw) with the bundled pythonw.exe in a clean environment: PATH
// is Windows' own folders only, so nothing installed on the computer (Python, FFmpeg, DLLs) is
// reached. The signing key, the settings and the APKs stay next to this .exe (T3B_HOME); the
// conversions in %LOCALAPPDATA%\Tekken3Builder\praca (T3B_WORK).
//
//   "Kompilator Tekken 3.exe"                                   the window
//   "Kompilator Tekken 3.exe" --build --jp X [--usa Y] [--arcade Z] --out DIR [--log FILE]
//                                                               no window: the build, its exit code
// Without an archive at its end (the developer's folder) it runs the folder next to it instead.
#include <windows.h>
#include <shlobj.h>
#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char MAGIC[8] = {'T', '3', 'B', 'P', 'A', 'K', '0', '1'};
const wchar_t TITLE[] = L"Kompilator Tekken 3";

struct Entry {
    std::wstring path;          // relative, backslashes
    uint64_t offset, packed, size;
    uint32_t crc;
    uint8_t method;             // 0 stored, 8 raw deflate
};

struct Archive {
    std::wstring exe;
    std::wstring id;
    std::vector<Entry> entries;
};

bool g_cli = false;
HANDLE g_log = INVALID_HANDLE_VALUE;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

void report(const std::wstring& text) {
    if (g_cli) {
        std::string line = narrow(text) + "\r\n";
        HANDLE out = g_log != INVALID_HANDLE_VALUE ? g_log : GetStdHandle(STD_ERROR_HANDLE);
        DWORD written;
        if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, line.data(), (DWORD)line.size(), &written, nullptr);
    } else {
        MessageBoxW(nullptr, text.c_str(), TITLE, MB_ICONERROR);
    }
}

std::wstring module_path() {
    std::wstring path(32768, 0);
    DWORD n = GetModuleFileNameW(nullptr, &path[0], (DWORD)path.size());
    path.resize(n);
    return path;
}

std::wstring parent(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool make_dirs(const std::wstring& dir) {
    if (dir.empty()) return true;
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!make_dirs(parent(dir))) return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool read_at(HANDLE f, uint64_t offset, void* buffer, DWORD size) {
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)offset;
    DWORD got = 0;
    return SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) && ReadFile(f, buffer, size, &got, nullptr) && got == size;
}

template <typename T> T take(const std::vector<char>& b, size_t& p) {
    T v;
    memcpy(&v, b.data() + p, sizeof v);
    p += sizeof v;
    return v;
}

// The archive's index from the trailer at the end of this file; false without one.
bool open_archive(Archive& a) {
    a.exe = module_path();
    HANDLE f = CreateFileW(a.exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    GetFileSizeEx(f, &size);
    char trailer[40];
    bool ok = size.QuadPart > 40 && read_at(f, size.QuadPart - 40, trailer, 40) && memcmp(trailer, MAGIC, 8) == 0;
    if (ok) {
        uint64_t index_offset, index_size;
        memcpy(&index_offset, trailer + 8, 8);
        memcpy(&index_size, trailer + 16, 8);
        a.id = widen(std::string(trailer + 24, 16));
        std::vector<char> index(index_size);
        ok = read_at(f, index_offset, index.data(), (DWORD)index_size);
        size_t p = 0;
        uint32_t count = ok ? take<uint32_t>(index, p) : 0;
        for (uint32_t i = 0; ok && i < count; i++) {
            Entry e;
            uint16_t n = take<uint16_t>(index, p);
            std::wstring path = widen(std::string(index.data() + p, n));
            p += n;
            for (auto& c : path) if (c == L'/') c = L'\\';
            e.path = path;
            e.offset = take<uint64_t>(index, p);
            e.packed = take<uint64_t>(index, p);
            e.size = take<uint64_t>(index, p);
            e.crc = take<uint32_t>(index, p);
            e.method = take<uint8_t>(index, p);
            a.entries.push_back(e);
        }
    }
    CloseHandle(f);
    return ok;
}

std::wstring local_app_data() {
    wchar_t* path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) out = path;
    CoTaskMemFree(path);
    return out + L"\\Tekken3Builder";
}

bool file_size_is(const std::wstring& path, uint64_t size) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return false;
    return ((uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow) == size;
}

// A small window that shows the unpacking's progress (the first start, or after damage).
struct Progress {
    HWND window = nullptr, label = nullptr;
    void open() {
        if (g_cli) return;
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"T3BuilderUnpack";
        wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 24));
        RegisterClassW(&wc);
        window = CreateWindowExW(0, wc.lpszClassName, TITLE, WS_POPUP | WS_BORDER | WS_VISIBLE,
                                 (GetSystemMetrics(SM_CXSCREEN) - 460) / 2, (GetSystemMetrics(SM_CYSCREEN) - 90) / 2,
                                 460, 90, nullptr, nullptr, wc.hInstance, nullptr);
        label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 32, 440, 30, window,
                                nullptr, wc.hInstance, nullptr);
        HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                 L"Bahnschrift");
        SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
    }
    void show(int percent) {
        if (!window) return;
        wchar_t text[96];
        swprintf(text, 96, L"Przygotowanie kompilatora: %d%%", percent);
        SetWindowTextW(label, text);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    void close() {
        if (window) DestroyWindow(window);
        window = nullptr;
    }
};

bool unpack_one(HANDLE in, const Entry& e, const std::wstring& root, std::wstring& error) {
    std::wstring dest = root + L"\\" + e.path;
    if (!make_dirs(parent(dest))) {
        error = L"nie można utworzyć folderu " + parent(dest);
        return false;
    }
    std::wstring part = dest + L".part";
    HANDLE out = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        error = L"nie można zapisać " + dest;
        return false;
    }
    z_stream z = {};
    if (e.method == 8) inflateInit2(&z, -15);
    std::vector<unsigned char> src(1 << 20), dst(1 << 20);
    uint64_t left = e.packed, written = 0;
    uint32_t crc = crc32(0, nullptr, 0);
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)e.offset;
    SetFilePointerEx(in, pos, nullptr, FILE_BEGIN);
    bool ok = true;
    while (ok && left > 0) {
        DWORD chunk = (DWORD)(left < src.size() ? left : src.size()), got = 0;
        if (!ReadFile(in, src.data(), chunk, &got, nullptr) || got != chunk) { ok = false; break; }
        left -= chunk;
        if (e.method == 0) {
            DWORD w;
            ok = WriteFile(out, src.data(), chunk, &w, nullptr) && w == chunk;
            crc = crc32(crc, src.data(), chunk);
            written += chunk;
            continue;
        }
        z.next_in = src.data();
        z.avail_in = chunk;
        do {
            z.next_out = dst.data();
            z.avail_out = (uInt)dst.size();
            int r = inflate(&z, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) { ok = false; break; }
            DWORD n = (DWORD)(dst.size() - z.avail_out), w;
            if (n) {
                ok = WriteFile(out, dst.data(), n, &w, nullptr) && w == n;
                crc = crc32(crc, dst.data(), n);
                written += n;
            }
        } while (ok && z.avail_out == 0);
    }
    if (e.method == 8) inflateEnd(&z);
    CloseHandle(out);
    if (!ok || written != e.size || crc != e.crc) {
        DeleteFileW(part.c_str());
        error = L"uszkodzony plik w kompilatorze: " + e.path + L" (pobierz kompilator ponownie)";
        return false;
    }
    if (!MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = L"nie można zapisać " + dest;
        return false;
    }
    return true;
}

// The unpacked builder of this archive, unpacked now if any file is missing or of another size.
bool ensure_unpacked(const Archive& a, const std::wstring& root) {
    std::wstring marker = root + L"\\.gotowe";
    bool complete = GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES;
    for (size_t i = 0; complete && i < a.entries.size(); i++)
        complete = file_size_is(root + L"\\" + a.entries[i].path, a.entries[i].size);
    if (complete) return true;

    DeleteFileW(marker.c_str());
    HANDLE in = CreateFileW(a.exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) {
        report(L"Nie można odczytać pliku kompilatora.");
        return false;
    }
    Progress progress;
    progress.open();
    uint64_t total = 0, done = 0;
    for (const Entry& e : a.entries) total += e.size;
    std::wstring error;
    bool ok = true;
    for (const Entry& e : a.entries) {
        if (!(ok = unpack_one(in, e, root, error))) break;
        done += e.size;
        progress.show((int)(done * 100 / (total ? total : 1)));
    }
    CloseHandle(in);
    progress.close();
    if (!ok) {
        report(L"Nie udało się przygotować kompilatora: " + error);
        return false;
    }
    HANDLE m = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    std::string id = narrow(a.id);
    DWORD w;
    WriteFile(m, id.data(), (DWORD)id.size(), &w, nullptr);
    CloseHandle(m);
    return true;
}

void delete_tree(const std::wstring& dir) {
    std::wstring from = dir + L'\0';       // SHFileOperation wants a double terminator
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

// Older versions' unpacked folders (app-<other id>).
void remove_old_versions(const std::wstring& base, const std::wstring& keep) {
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((base + L"\\app-*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = d.cFileName;
        if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != keep) delete_tree(base + L"\\" + name);
    } while (FindNextFileW(h, &d));
    FindClose(h);
}

std::wstring quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { slashes++; continue; }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

// The environment the builder runs in: Windows' own variables and folders, nothing else.
std::wstring clean_environment(const std::wstring& home, const std::wstring& work) {
    static const wchar_t* KEEP[] = {L"SystemRoot", L"SystemDrive", L"windir", L"TEMP", L"TMP", L"USERPROFILE",
                                    L"LOCALAPPDATA", L"APPDATA", L"HOMEDRIVE", L"HOMEPATH", L"USERNAME",
                                    L"USERDOMAIN", L"COMPUTERNAME", L"ComSpec", L"PROCESSOR_ARCHITECTURE",
                                    L"PROCESSOR_IDENTIFIER", L"NUMBER_OF_PROCESSORS", L"OS", L"PATHEXT",
                                    L"ProgramData", L"ProgramFiles", L"PUBLIC"};
    std::wstring block;
    wchar_t value[32768];
    for (const wchar_t* name : KEEP) {
        DWORD n = GetEnvironmentVariableW(name, value, 32768);
        if (n && n < 32768) block += std::wstring(name) + L"=" + value + L'\0';
    }
    GetEnvironmentVariableW(L"SystemRoot", value, 32768);
    std::wstring windows = value;
    block += L"PATH=" + windows + L"\\System32;" + windows + L";" + windows + L"\\System32\\Wbem" + L'\0';
    block += L"PYTHONUTF8=1";
    block += L'\0';
    block += L"T3B_HOME=" + home + L'\0';
    block += L"T3B_WORK=" + work + L'\0';
    return block + L'\0';
}

int run(const std::wstring& root, const std::wstring& home, const std::wstring& work, int argc, wchar_t** argv) {
    std::wstring env = clean_environment(home, work);
    std::wstring python = root + L"\\runtime\\python\\";
    std::wstring line;
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (!g_cli) {
        line = quote(python + L"pythonw.exe") + L" -E -s " + quote(root + L"\\app\\kompilator.pyw");
    } else {
        line = quote(python + L"python.exe") + L" -E -s -u -m t3builder.pipeline";
        for (int i = 2; i < argc; i++) {
            if (std::wstring(argv[i]) == L"--log" && i + 1 < argc) { i++; continue; }
            line += L" " + quote(argv[i]);
        }
        si.dwFlags = STARTF_USESTDHANDLES;
        HANDLE out = g_log != INVALID_HANDLE_VALUE ? g_log : GetStdHandle(STD_OUTPUT_HANDLE);
        // A real (empty) input: Python's subprocess duplicates the parent's stdin for every child
        // it starts (FFmpeg), and an invalid handle fails that with WinError 6.
        SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
        si.hStdInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        si.hStdOutput = out;
        si.hStdError = out;
        flags |= CREATE_NO_WINDOW;
    }
    std::wstring cwd = root + L"\\app";
    if (!CreateProcessW(nullptr, &line[0], nullptr, nullptr, g_cli, flags, &env[0], cwd.c_str(), &si, &pi)) {
        report(L"Nie można uruchomić " + python + L" (kod " + std::to_wstring(GetLastError()) + L").");
        return 1;
    }
    DWORD code = 0;
    if (g_cli) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_cli = argc > 1 && std::wstring(argv[1]) == L"--build";
    for (int i = 2; g_cli && i + 1 < argc; i++) {
        if (std::wstring(argv[i]) == L"--log") {
            SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
            g_log = CreateFileW(argv[i + 1], GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        }
    }
    std::wstring home = parent(module_path());
    Archive archive;
    std::wstring root, work;
    if (open_archive(archive)) {
        std::wstring base = local_app_data();
        root = base + L"\\app-" + archive.id;
        work = base + L"\\praca";
        if (!make_dirs(root) || !ensure_unpacked(archive, root)) return 1;
        remove_old_versions(base, L"app-" + archive.id);
    } else {
        root = home;                                   // the developer's folder
        work = home + L"\\praca";
    }
    int code = run(root, home, work, argc, argv);
    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    return code;
}
