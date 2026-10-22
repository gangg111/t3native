/* The builder's launcher: runs the window (app\kompilator.pyw) with the bundled Python, from the
   folder this .exe lies in. A Windows (GUI) program, so no console window opens. */
#include <windows.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR args, int show) {
    (void)instance; (void)previous; (void)args; (void)show;
    wchar_t dir[32768];
    DWORD n = GetModuleFileNameW(NULL, dir, 32768);
    if (n == 0 || n >= 32768) return 1;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    static wchar_t line[3 * 32768];
    /* -E: ignore PYTHONPATH and PYTHONHOME of the user's own Python; -s: no user site-packages. */
    swprintf(line, sizeof line / sizeof line[0],
             L"\"%ls\\runtime\\python\\pythonw.exe\" -E -s \"%ls\\app\\kompilator.pyw\"", dir, dir);
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        MessageBoxW(NULL, L"Nie można uruchomić runtime\\python\\pythonw.exe. "
                    L"Rozpakuj cały folder kompilatora i uruchom go z niego.",
                    L"Kompilator Tekken 3", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
