#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define SCAN_A       0x1E
#define SCAN_D       0x20

/* Через сколько миллисекунд после последнего горизонтального
   движения мыши отпустить A/D. */
#define IDLE_TIMEOUT_MS 100

/* 1 = выводить состояние в консоль.
   Для минимальной задержки можно поставить 0. */
#define DEBUG_OUTPUT 1

typedef enum {
    DIR_NONE = 0,
    DIR_A,
    DIR_D
} Direction;

static HWND g_hwnd = NULL;

static int g_space_down = 0;

static Direction g_current_dir = DIR_NONE;

static LARGE_INTEGER g_qpc_freq;
static LARGE_INTEGER g_last_mouse_move;

/* -----------------------------------------------------------
   High-resolution timer
   ----------------------------------------------------------- */

static uint64_t qpc_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)t.QuadPart;
}

static uint64_t qpc_ms(void)
{
    return (uint64_t)g_qpc_freq.QuadPart / 1000ULL;
}

static uint64_t elapsed_ms(uint64_t start)
{
    uint64_t now = qpc_now();

    if (now <= start)
        return 0;

    return ((now - start) * 1000ULL) /
           (uint64_t)g_qpc_freq.QuadPart;
}

static int idle_timeout_expired(void)
{
    if (!g_space_down)
        return 1;

    if (g_current_dir == DIR_NONE)
        return 0;

    return elapsed_ms((uint64_t)g_last_mouse_move.QuadPart)
           >= IDLE_TIMEOUT_MS;
}

/* -----------------------------------------------------------
   Console output
   ----------------------------------------------------------- */

static const char *direction_name(Direction dir)
{
    switch (dir) {
        case DIR_A:    return "A";
        case DIR_D:    return "D";
        default:       return "-";
    }
}

static void print_state(void)
{
#if DEBUG_OUTPUT
    printf("\rOutput: [%s]        ", direction_name(g_current_dir));
    fflush(stdout);
#endif
}

/* -----------------------------------------------------------
   Keyboard injection
   ----------------------------------------------------------- */

static int send_key_event(BYTE scan, DWORD flags)
{
    INPUT input;

    ZeroMemory(&input, sizeof(input));

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = scan;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | flags;

    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

/*
   Важно:
   При смене направления release + press идут одним SendInput(2).
   Благодаря этому между ними нет отдельного вызова SendInput().
*/
static int switch_direction(Direction new_dir)
{
    INPUT inputs[2];
    UINT count = 0;

    if (new_dir == g_current_dir)
        return 1;

    ZeroMemory(inputs, sizeof(inputs));

    /* -------------------------------------------------------
       Release текущей клавиши
       ------------------------------------------------------- */

    if (g_current_dir == DIR_A) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 0;
        inputs[count].ki.wScan = SCAN_A;
        inputs[count].ki.dwFlags =
            KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        count++;
    }
    else if (g_current_dir == DIR_D) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 0;
        inputs[count].ki.wScan = SCAN_D;
        inputs[count].ki.dwFlags =
            KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        count++;
    }

    /* -------------------------------------------------------
       Press новой клавиши
       ------------------------------------------------------- */

    if (new_dir == DIR_A) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 0;
        inputs[count].ki.wScan = SCAN_A;
        inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;

        count++;
    }
    else if (new_dir == DIR_D) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 0;
        inputs[count].ki.wScan = SCAN_D;
        inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;

        count++;
    }

    if (count == 0) {
        g_current_dir = DIR_NONE;
        print_state();
        return 1;
    }

    UINT sent = SendInput(count, inputs, sizeof(INPUT));

    if (sent != count) {
#if DEBUG_OUTPUT
        printf("\nSendInput failed: sent=%u expected=%u error=%lu\n",
               sent,
               count,
               GetLastError());
#endif

        /*
           Состояние могло частично измениться.
           Для безопасности принудительно отпускаем обе клавиши.
        */
        INPUT release[2];

        ZeroMemory(release, sizeof(release));

        release[0].type = INPUT_KEYBOARD;
        release[0].ki.wVk = 0;
        release[0].ki.wScan = SCAN_A;
        release[0].ki.dwFlags =
            KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        release[1].type = INPUT_KEYBOARD;
        release[1].ki.wVk = 0;
        release[1].ki.wScan = SCAN_D;
        release[1].ki.dwFlags =
            KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        SendInput(2, release, sizeof(INPUT));

        g_current_dir = DIR_NONE;
        print_state();

        return 0;
    }

    g_current_dir = new_dir;
    print_state();

    return 1;
}

static void release_all(void)
{
    INPUT inputs[2];

    ZeroMemory(inputs, sizeof(inputs));

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0;
    inputs[0].ki.wScan = SCAN_A;
    inputs[0].ki.dwFlags =
        KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0;
    inputs[1].ki.wScan = SCAN_D;
    inputs[1].ki.dwFlags =
        KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));

    g_current_dir = DIR_NONE;
}

/* -----------------------------------------------------------
   Raw input
   ----------------------------------------------------------- */

static void process_raw_input(HRAWINPUT hraw)
{
    UINT size = 0;

    if (GetRawInputData(
            hraw,
            RID_INPUT,
            NULL,
            &size,
            sizeof(RAWINPUTHEADER)
        ) == (UINT)-1) {
        return;
    }

    if (size == 0)
        return;

    /*
       Для mouse + keyboard RAWINPUT этого размера достаточно.
       Но если Windows неожиданно отдаст больше, выделим память.
    */
    BYTE local_buffer[sizeof(RAWINPUT)];
    BYTE *buffer = local_buffer;
    int allocated = 0;

    if (size > sizeof(local_buffer)) {
        buffer = (BYTE *)HeapAlloc(
            GetProcessHeap(),
            0,
            size
        );

        if (!buffer)
            return;

        allocated = 1;
    }

    UINT got = GetRawInputData(
        hraw,
        RID_INPUT,
        buffer,
        &size,
        sizeof(RAWINPUTHEADER)
    );

    if (got != size) {
        if (allocated)
            HeapFree(GetProcessHeap(), 0, buffer);

        return;
    }

    RAWINPUT *raw = (RAWINPUT *)buffer;

    /* -------------------------------------------------------
       Keyboard
       ------------------------------------------------------- */

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {

        RAWKEYBOARD *kb = &raw->data.keyboard;

        /*
           Space:
           MakeCode обычно 0x39,
           но надёжнее ориентироваться на VKey.
        */
        if (kb->VKey == VK_SPACE) {

            int is_break =
                (kb->Flags & RI_KEY_BREAK) != 0;

            if (!is_break) {
                g_space_down = 1;
            }
            else {
                g_space_down = 0;
                switch_direction(DIR_NONE);
            }
        }
    }

    /* -------------------------------------------------------
       Mouse
       ------------------------------------------------------- */

    else if (raw->header.dwType == RIM_TYPEMOUSE) {

        RAWMOUSE *mouse = &raw->data.mouse;

        /*
           usFlags — битовое поле.
           MOUSE_MOVE_RELATIVE == 0, поэтому нельзя делать
           простое сравнение на равенство.
        */
        if (!(mouse->usFlags & MOUSE_MOVE_ABSOLUTE)) {

            LONG dx = mouse->lLastX;

            /*
               Нас интересует только горизонтальное движение.
            */
            if (dx != 0) {

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);

                g_last_mouse_move = now;

                /*
                   Если Space не зажат, мышь ничего не делает.
                */
                if (g_space_down) {

                    Direction wanted =
                        (dx < 0)
                            ? DIR_A
                            : DIR_D;

                    /*
                       Переключение происходит только тогда,
                       когда направление реально изменилось.
                    */
                    switch_direction(wanted);
                }
            }
        }
    }

    if (allocated)
        HeapFree(GetProcessHeap(), 0, buffer);
}

/* -----------------------------------------------------------
   Window procedure
   ----------------------------------------------------------- */

static LRESULT CALLBACK wnd_proc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg) {

        case WM_INPUT:
            process_raw_input((HRAWINPUT)lParam);

            /*
               Важно:
               после обработки WM_INPUT передаём сообщение
               DefWindowProc для системной очистки.
            */
            return DefWindowProcW(
                hwnd,
                msg,
                wParam,
                lParam
            );

        case WM_DESTROY:
            release_all();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* -----------------------------------------------------------
   Console handler
   ----------------------------------------------------------- */

static BOOL WINAPI console_handler(DWORD type)
{
    switch (type) {

        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:

            if (g_hwnd) {
                PostMessageW(
                    g_hwnd,
                    WM_CLOSE,
                    0,
                    0
                );

                return TRUE;
            }

            break;
    }

    return FALSE;
}

/* -----------------------------------------------------------
   Main
   ----------------------------------------------------------- */

int main(void)
{
    printf("Auto-strafe by raw mouse input\n");
    printf("==============================\n\n");
    printf("Hold SPACE: mouse left -> A, mouse right -> D\n");
    printf("Idle timeout: %d ms\n\n", IDLE_TIMEOUT_MS);

    /* High resolution timer */
    if (!QueryPerformanceFrequency(&g_qpc_freq)) {
        printf("QueryPerformanceFrequency failed.\n");
        return 1;
    }

    QueryPerformanceCounter(&g_last_mouse_move);

    /* Начальное состояние Space */
    g_space_down =
        (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

    SetConsoleCtrlHandler(
        console_handler,
        TRUE
    );

    /* -------------------------------------------------------
       Window class
       ------------------------------------------------------- */

    WNDCLASSW wc;

    ZeroMemory(&wc, sizeof(wc));

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"AutoStrafeWnd";

    if (!RegisterClassW(&wc)) {
        printf(
            "RegisterClassW failed: %lu\n",
            GetLastError()
        );
        return 1;
    }

    /*
       Невидимое окно.
       Оно существует только для получения WM_INPUT.
    */
    g_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"AutoStrafe",
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        NULL,
        NULL,
        wc.hInstance,
        NULL
    );

    if (!g_hwnd) {
        printf(
            "CreateWindowExW failed: %lu\n",
            GetLastError()
        );
        return 1;
    }

    /* -------------------------------------------------------
       Raw Input registration
       ------------------------------------------------------- */

    RAWINPUTDEVICE rid[2];

    ZeroMemory(rid, sizeof(rid));

    /* Mouse */
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = g_hwnd;

    /* Keyboard */
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = g_hwnd;

    if (!RegisterRawInputDevices(
            rid,
            2,
            sizeof(RAWINPUTDEVICE))) {

        printf(
            "RegisterRawInputDevices failed: %lu\n",
            GetLastError()
        );

        DestroyWindow(g_hwnd);
        return 1;
    }

    print_state();

    /* -------------------------------------------------------
       Event loop
       ------------------------------------------------------- */

    for (;;) {

        DWORD timeout = INFINITE;

        /*
           Если A/D сейчас удерживается, ждём до момента,
           когда истечёт IDLE_TIMEOUT_MS.
        */
        if (g_space_down &&
            g_current_dir != DIR_NONE) {

            uint64_t elapsed =
                elapsed_ms(
                    (uint64_t)g_last_mouse_move.QuadPart
                );

            if (elapsed >= IDLE_TIMEOUT_MS) {
                timeout = 0;
            }
            else {
                DWORD remaining =
                    (DWORD)(IDLE_TIMEOUT_MS - elapsed);

                /*
                   Не даём уйти в 0 ms раньше срока.
                */
                if (remaining == 0)
                    remaining = 1;

                timeout = remaining;
            }
        }

        DWORD result = MsgWaitForMultipleObjectsEx(
            0,
            NULL,
            timeout,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE
        );

        if (result == WAIT_FAILED) {
            printf(
                "\nMsgWaitForMultipleObjectsEx failed: %lu\n",
                GetLastError()
            );
            break;
        }

        /*
           Обрабатываем все накопившиеся сообщения.
        */
        MSG msg;

        while (PeekMessageW(
                   &msg,
                   NULL,
                   0,
                   0,
                   PM_REMOVE)) {

            if (msg.message == WM_QUIT)
                goto exit_loop;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        /*
           Проверяем idle после обработки сообщений.
           Это важно, если очередь сообщений постоянно занята.
        */
        if (g_space_down &&
            g_current_dir != DIR_NONE &&
            idle_timeout_expired()) {

            switch_direction(DIR_NONE);

#if DEBUG_OUTPUT
            printf("\rIdle: keys released      ");
            fflush(stdout);
#endif
        }
    }

exit_loop:

    release_all();

    printf("\nExiting.\n");

    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }

    return 0;
}