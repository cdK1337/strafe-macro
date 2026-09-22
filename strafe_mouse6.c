#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define SCAN_A          0x1E
#define SCAN_D          0x20
#define SCAN_W          0x11
#define SCAN_S          0x1F
#define IDLE_TIMEOUT_MS 100

#define WM_APP_RECONCILE (WM_APP + 1)

/*
    1 = вывод состояния в консоль
    0 = минимум лишней нагрузки
*/
#define DEBUG_OUTPUT 1

typedef enum {
    DIR_NONE = 0,
    DIR_A,
    DIR_D
} Direction;

/* -----------------------------------------------------------
   Global state
   ----------------------------------------------------------- */

static HWND g_hwnd = NULL;
static HHOOK g_keyboard_hook = NULL;

/* Физические A/D */
static volatile LONG g_physical_a = 0;
static volatile LONG g_physical_d = 0;

/* Физические W/S */
static volatile LONG g_physical_w = 0;
static volatile LONG g_physical_s = 0;

/* Физический Space */
static volatile LONG g_space_down = 0;

/* Направление, полученное от мыши */
static volatile LONG g_mouse_dir = DIR_NONE;

/*
   Клавиша, которую именно скрипт сейчас удерживает.
   Здесь никогда не учитываются физические нажатия.
*/
static Direction g_script_dir = DIR_NONE;

/* Время последнего горизонтального движения мыши */
static LARGE_INTEGER g_last_mouse_move;

/* Частота QPC */
static LARGE_INTEGER g_qpc_freq;


/* -----------------------------------------------------------
   Helpers
   ----------------------------------------------------------- */

static uint64_t qpc_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)t.QuadPart;
}

static uint64_t elapsed_ms_from(LARGE_INTEGER *start)
{
    uint64_t now = qpc_now();

    if (now <= (uint64_t)start->QuadPart)
        return 0;

    return ((now - (uint64_t)start->QuadPart) * 1000ULL) /
           (uint64_t)g_qpc_freq.QuadPart;
}

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
    printf(
        "\rOutput: [%s]   Physical: A=%ld D=%ld W=%ld S=%ld Space=%ld        ",
        direction_name(g_script_dir),
        g_physical_a,
        g_physical_d,
        g_physical_w,
        g_physical_s,
        g_space_down
    );
    fflush(stdout);
#endif
}

static void post_reconcile(void)
{
    if (g_hwnd != NULL) {
        PostMessageW(
            g_hwnd,
            WM_APP_RECONCILE,
            0,
            0
        );
    }
}


/* -----------------------------------------------------------
   Script keyboard injection
   ----------------------------------------------------------- */

/*
    Важный принцип:

    g_script_dir = клавиша, которую удерживает ИМЕННО скрипт.

    Физический A/D сюда никогда не записываются.
*/

static int send_script_transition(Direction new_dir)
{
    INPUT inputs[2];
    UINT count = 0;

    if (new_dir == g_script_dir)
        return 1;

    ZeroMemory(inputs, sizeof(inputs));

    /* -------------------------------------------------------
       Release current script key
       ------------------------------------------------------- */

    if (g_script_dir == DIR_A) {

        /*
           Если реальный A сейчас зажат, нельзя отправлять
           KEYUP A — это может выглядеть для игры как
           отпускание физического A.

           В нормальной логике сюда такой переход не попадёт,
           но эта проверка является дополнительной защитой.
        */
        if (!g_physical_a) {

            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = 0;
            inputs[count].ki.wScan = SCAN_A;
            inputs[count].ki.dwFlags =
                KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            count++;
        }
    }
    else if (g_script_dir == DIR_D) {

        if (!g_physical_d) {

            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = 0;
            inputs[count].ki.wScan = SCAN_D;
            inputs[count].ki.dwFlags =
                KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            count++;
        }
    }

    /* -------------------------------------------------------
       Press new script key
       ------------------------------------------------------- */

    if (new_dir == DIR_A) {

        /*
           Не создаём синтетический A, если пользователь уже
           реально держит A.
        */
        if (!g_physical_a) {

            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = 0;
            inputs[count].ki.wScan = SCAN_A;
            inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;

            count++;
        }
    }
    else if (new_dir == DIR_D) {

        if (!g_physical_d) {

            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = 0;
            inputs[count].ki.wScan = SCAN_D;
            inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;

            count++;
        }
    }

    /*
       Если физическая клавиша полностью обеспечивает
       нужное состояние, SendInput вообще не нужен.
    */
    if (count == 0) {
        g_script_dir = new_dir;
        print_state();
        return 1;
    }

    /*
       Самое важное:
       A -> D и D -> A идут одним SendInput().
    */
    UINT sent = SendInput(
        count,
        inputs,
        sizeof(INPUT)
    );

    if (sent != count) {

#if DEBUG_OUTPUT
        printf(
            "\nSendInput failed: sent=%u expected=%u error=%lu\n",
            sent,
            count,
            GetLastError()
        );
#endif

        /*
           Не пытаемся "аварийно отпустить обе клавиши":
           это может затронуть реальное A/D пользователя.

           При частичной отправке обновляем только то,
           что достоверно произошло.

           Например:
             A -> D
             sent == 1
             значит KEYUP A прошёл,
             KEYDOWN D — нет.
        */

        if (count == 2 && sent == 1) {

            if (g_script_dir == DIR_A &&
                !g_physical_a) {

                g_script_dir = DIR_NONE;
            }
            else if (g_script_dir == DIR_D &&
                     !g_physical_d) {

                g_script_dir = DIR_NONE;
            }
        }

        print_state();
        return 0;
    }

    g_script_dir = new_dir;

    print_state();

    return 1;
}


/* -----------------------------------------------------------
   Reconcile logic
   ----------------------------------------------------------- */

/*
    Основная логика приоритетов.

    1. Реальные A/D имеют приоритет.
    2. Если пользователь держит только A:
         скрипт может продолжать держать A,
         но никогда не переключится на D.
    3. Аналогично для D.
    4. Если A и D одновременно физически нажаты:
         скрипт ничего не меняет.
    5. Если физических A/D нет:
         работает mouse -> script.
*/
static void reconcile_script(void)
{
    int physical_a = (g_physical_a != 0);
    int physical_d = (g_physical_d != 0);

    /* -------------------------------------------------------
       Пользователь физически держит W и/или S
       ------------------------------------------------------- */

    if (g_physical_w || g_physical_s) {

        /*
           W/S имеют приоритет: скрипт не мешает движению
           вперёд/назад. Если скрипт сейчас держит A/D,
           отпускаем их безопасно.
        */
        if (g_script_dir != DIR_NONE) {
            send_script_transition(DIR_NONE);
        }

        return;
    }

    /* -------------------------------------------------------
       Пользователь физически держит A
       ------------------------------------------------------- */

    if (physical_a && !physical_d) {

        /*
           Скрипт уже держит A:
           НЕ отправляем KEYUP A.
        */
        if (g_script_dir == DIR_A)
            return;

        /*
           Скрипт держит D:
           A физический имеет приоритет,
           поэтому можно безопасно отпустить D.
        */
        if (g_script_dir == DIR_D) {
            send_script_transition(DIR_NONE);
        }

        return;
    }

    /* -------------------------------------------------------
       Пользователь физически держит D
       ------------------------------------------------------- */

    if (physical_d && !physical_a) {

        /*
           Скрипт уже держит D:
           ничего не делаем.
        */
        if (g_script_dir == DIR_D)
            return;

        /*
           Скрипт держит A:
           можно безопасно отпустить A.
        */
        if (g_script_dir == DIR_A) {
            send_script_transition(DIR_NONE);
        }

        return;
    }

    /* -------------------------------------------------------
       Пользователь физически держит и A, и D
       ------------------------------------------------------- */

    if (physical_a && physical_d) {

        /*
           Здесь вообще ничего не меняем.

           Реальные A/D уже идут в игру.
           Скрипт сохраняет своё текущее состояние, если
           оно было, чтобы не отправлять KEYUP поверх
           физического нажатия.
        */
        return;
    }

    /* -------------------------------------------------------
       Ни A, ни D физически не нажаты
       ------------------------------------------------------- */

    Direction desired = DIR_NONE;

    if (g_space_down) {

        Direction mouse_dir =
            (Direction)g_mouse_dir;

        if (mouse_dir != DIR_NONE) {

            if (elapsed_ms_from(&g_last_mouse_move)
                < IDLE_TIMEOUT_MS) {

                desired = mouse_dir;
            }
        }
    }

    if (desired != g_script_dir) {
        send_script_transition(desired);
    }
}


/* -----------------------------------------------------------
   Low-level keyboard hook
   ----------------------------------------------------------- */

static LRESULT CALLBACK low_level_keyboard_proc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam)
{
    if (nCode < 0) {
        return CallNextHookEx(
            g_keyboard_hook,
            nCode,
            wParam,
            lParam
        );
    }

    KBDLLHOOKSTRUCT *kbd =
        (KBDLLHOOKSTRUCT *)lParam;

    /*
       Это КЛЮЧЕВОЕ место.

       SendInput() помечает свои события
       флагом LLKHF_INJECTED.

       Поэтому такие события не считаем
       физическими нажатиями пользователя.
    */
    if (kbd->flags & LLKHF_INJECTED) {

        return CallNextHookEx(
            g_keyboard_hook,
            nCode,
            wParam,
            lParam
        );
    }

    int is_up =
        (kbd->flags & LLKHF_UP) != 0;

    DWORD scan = kbd->scanCode;

    /* -------------------------------------------------------
       Physical A
       ------------------------------------------------------- */

    if (scan == SCAN_A) {

        LONG old = g_physical_a;

        if (is_up)
            g_physical_a = 0;
        else
            g_physical_a = 1;

        if (old != g_physical_a) {

            if (!is_up) {

                /*
                   Очень важно:
                   конфликтующую клавишу отпускаем
                   сразу в hook, не ждём message loop.

                   Например:
                     script D held
                     user presses physical A
                         ↓
                     KEYUP D
                         ↓
                     physical A -> game
                */
                if (g_physical_a &&
                    !g_physical_d &&
                    g_script_dir == DIR_D) {

                    send_script_transition(DIR_NONE);
                }
            }
            else {

                /*
                   При физическом отпускании не обязательно
                   ждать следующий WM_INPUT.
                   Просим message loop вернуть управление
                   скрипту.
                */
                post_reconcile();
            }
        }
    }

    /* -------------------------------------------------------
       Physical D
       ------------------------------------------------------- */

    else if (scan == SCAN_D) {

        LONG old = g_physical_d;

        if (is_up)
            g_physical_d = 0;
        else
            g_physical_d = 1;

        if (old != g_physical_d) {

            if (!is_up) {

                if (g_physical_d &&
                    !g_physical_a &&
                    g_script_dir == DIR_A) {

                    send_script_transition(DIR_NONE);
                }
            }
            else {
                post_reconcile();
            }
        }
    }

    /* -------------------------------------------------------
       Physical W
       ------------------------------------------------------- */

    else if (scan == SCAN_W) {

        if (is_up)
            g_physical_w = 0;
        else
            g_physical_w = 1;

        if (g_physical_w) {
            reconcile_script();
            post_reconcile();
        }
    }

    /* -------------------------------------------------------
       Physical S
       ------------------------------------------------------- */

    else if (scan == SCAN_S) {

        if (is_up)
            g_physical_s = 0;
        else
            g_physical_s = 1;

        if (!is_up) {
            reconcile_script();
        }
        else {
            post_reconcile();
        }
    }

    /* -------------------------------------------------------
       Physical SPACE
       ------------------------------------------------------- */

    else if (kbd->vkCode == VK_SPACE) {

        LONG old = g_space_down;

        if (is_up)
            g_space_down = 0;
        else
            g_space_down = 1;

        if (old != g_space_down) {

            if (!is_up) {

                /*
                   Новое нажатие Space начинается "с нуля".
                   Старое направление мыши не переносим.
                */
                g_mouse_dir = DIR_NONE;

                /*
                   Если скрипт в этот момент держит клавишу,
                   reconcile сам решит, можно ли её отпустить
                   безопасно.
                */
                reconcile_script();
            }
            else {

                /*
                   Space отпустили.

                   Здесь нельзя бездумно делать KEYUP:
                   если пользователь физически держит ту же
                   A/D, это может сломать его настоящее
                   нажатие.

                   reconcile это учитывает.
                */
                reconcile_script();

                post_reconcile();
            }
        }
    }

    return CallNextHookEx(
        g_keyboard_hook,
        nCode,
        wParam,
        lParam
    );
}


/* -----------------------------------------------------------
   Raw mouse input
   ----------------------------------------------------------- */

static void process_raw_mouse(HRAWINPUT hraw)
{
    UINT size = 0;

    UINT result = GetRawInputData(
        hraw,
        RID_INPUT,
        NULL,
        &size,
        sizeof(RAWINPUTHEADER)
    );

    if (result == (UINT)-1 || size == 0)
        return;

    BYTE stack_buffer[sizeof(RAWINPUT)];
    BYTE *buffer = stack_buffer;
    int allocated = 0;

    if (size > sizeof(stack_buffer)) {

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

        if (allocated) {
            HeapFree(
                GetProcessHeap(),
                0,
                buffer
            );
        }

        return;
    }

    RAWINPUT *raw =
        (RAWINPUT *)buffer;

    if (raw->header.dwType == RIM_TYPEMOUSE) {

        RAWMOUSE *mouse =
            &raw->data.mouse;

        /*
           usFlags — битовое поле.
           Проверяем именно ABSOLUTE bit.
        */
        if (!(mouse->usFlags & MOUSE_MOVE_ABSOLUTE)) {

            LONG dx = mouse->lLastX;

            if (dx != 0 && g_space_down) {

                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);

                g_last_mouse_move = now;

                /*
                   Только последняя сторона мыши важна.
                */
                if (dx < 0)
                    g_mouse_dir = DIR_A;
                else
                    g_mouse_dir = DIR_D;

                /*
                   Даже если пользователь сейчас держит
                   реальный A/D, направление мыши запоминаем.

                   После отпускания физической клавиши
                   скрипт сможет продолжить в этом направлении,
                   пока не вышел IDLE_TIMEOUT.
                */
                reconcile_script();
            }
        }
    }

    if (allocated) {

        HeapFree(
            GetProcessHeap(),
            0,
            buffer
        );
    }
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

            process_raw_mouse(
                (HRAWINPUT)lParam
            );

            /*
               Передаём WM_INPUT дальше,
               чтобы Windows корректно завершила
               обработку raw input packet.
            */
            return DefWindowProcW(
                hwnd,
                msg,
                wParam,
                lParam
            );

        case WM_APP_RECONCILE:

            reconcile_script();
            return 0;

        case WM_DESTROY:

            /*
               При завершении процесса не отправляем
               KEYUP той клавиши, которую пользователь
               всё ещё физически держит.
            */
            if (g_script_dir == DIR_A) {

                if (!g_physical_a) {
                    INPUT input;
                    ZeroMemory(&input, sizeof(input));

                    input.type = INPUT_KEYBOARD;
                    input.ki.wScan = SCAN_A;
                    input.ki.dwFlags =
                        KEYEVENTF_SCANCODE |
                        KEYEVENTF_KEYUP;

                    SendInput(
                        1,
                        &input,
                        sizeof(INPUT)
                    );
                }
            }
            else if (g_script_dir == DIR_D) {

                if (!g_physical_d) {
                    INPUT input;
                    ZeroMemory(&input, sizeof(input));

                    input.type = INPUT_KEYBOARD;
                    input.ki.wScan = SCAN_D;
                    input.ki.dwFlags =
                        KEYEVENTF_SCANCODE |
                        KEYEVENTF_KEYUP;

                    SendInput(
                        1,
                        &input,
                        sizeof(INPUT)
                    );
                }
            }

            g_script_dir = DIR_NONE;

            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam
    );
}


/* -----------------------------------------------------------
   Main
   ----------------------------------------------------------- */

int main(void)
{
    printf("Auto-strafe by raw mouse input\n");
    printf("==============================\n\n");

    printf(
        "SPACE + mouse left  -> A\n"
        "SPACE + mouse right -> D\n"
        "Physical A/D have priority\n\n"
    );

    printf(
        "Idle timeout: %d ms\n\n",
        IDLE_TIMEOUT_MS
    );

    /* -------------------------------------------------------
       QPC
       ------------------------------------------------------- */

    if (!QueryPerformanceFrequency(&g_qpc_freq)) {

        printf(
            "QueryPerformanceFrequency failed.\n"
        );

        return 1;
    }

    QueryPerformanceCounter(
        &g_last_mouse_move
    );

    /* -------------------------------------------------------
       Window class
       ------------------------------------------------------- */

    WNDCLASSW wc;

    ZeroMemory(
        &wc,
        sizeof(wc)
    );

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance =
        GetModuleHandleW(NULL);
    wc.lpszClassName =
        L"AutoStrafeWnd";

    if (!RegisterClassW(&wc)) {

        printf(
            "RegisterClassW failed: %lu\n",
            GetLastError()
        );

        return 1;
    }

    /*
       Скрытое окно используется только для WM_INPUT
       и внутренних сообщений.
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
       Raw Input: только мышь
       ------------------------------------------------------- */

    RAWINPUTDEVICE rid;

    ZeroMemory(
        &rid,
        sizeof(rid)
    );

    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = g_hwnd;

    if (!RegisterRawInputDevices(
            &rid,
            1,
            sizeof(rid))) {

        printf(
            "RegisterRawInputDevices failed: %lu\n",
            GetLastError()
        );

        DestroyWindow(g_hwnd);
        return 1;
    }

    /* -------------------------------------------------------
       Low-level keyboard hook
       ------------------------------------------------------- */

    g_keyboard_hook =
        SetWindowsHookExW(
            WH_KEYBOARD_LL,
            low_level_keyboard_proc,
            NULL,
            0
        );

    if (!g_keyboard_hook) {

        printf(
            "SetWindowsHookExW failed: %lu\n",
            GetLastError()
        );

        DestroyWindow(g_hwnd);
        return 1;
    }

    printf(
        "Keyboard hook installed.\n"
    );

    print_state();

    /* -------------------------------------------------------
       Event loop
       ------------------------------------------------------- */

    for (;;) {

        DWORD timeout = INFINITE;

        /*
           Если скрипт держит A/D, ждём максимум
           до момента истечения IDLE_TIMEOUT.
        */
        if (g_script_dir != DIR_NONE &&
            !g_physical_a &&
            !g_physical_d) {

            uint64_t elapsed =
                elapsed_ms_from(
                    &g_last_mouse_move
                );

            if (elapsed >= IDLE_TIMEOUT_MS) {

                timeout = 0;
            }
            else {

                DWORD remaining =
                    (DWORD)(
                        IDLE_TIMEOUT_MS - elapsed
                    );

                if (remaining == 0)
                    remaining = 1;

                timeout = remaining;
            }
        }

        DWORD wait_result =
            MsgWaitForMultipleObjectsEx(
                0,
                NULL,
                timeout,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE
            );

        if (wait_result == WAIT_FAILED) {

            printf(
                "\nMsgWaitForMultipleObjectsEx failed: %lu\n",
                GetLastError()
            );

            break;
        }

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
           Проверяем idle после обработки всей очереди.
        */
        if (g_script_dir != DIR_NONE &&
            !g_physical_a &&
            !g_physical_d) {

            if (elapsed_ms_from(
                    &g_last_mouse_move
                ) >= IDLE_TIMEOUT_MS) {

                if (g_space_down) {

                    /*
                       Mouse idle.
                       Само состояние mouse_dir можно оставить —
                       при новом движении оно будет обновлено.
                    */
                    send_script_transition(
                        DIR_NONE
                    );

#if DEBUG_OUTPUT
                    printf(
                        "\rIdle: keys released                              "
                    );
                    fflush(stdout);
#endif
                }
            }
        }
    }

exit_loop:

    if (g_keyboard_hook) {

        UnhookWindowsHookEx(
            g_keyboard_hook
        );

        g_keyboard_hook = NULL;
    }

    /*
       Если скрипт ещё держит клавишу,
       освобождаем её только когда это безопасно.
    */
    if (g_script_dir == DIR_A) {

        if (!g_physical_a) {

            INPUT input;
            ZeroMemory(
                &input,
                sizeof(input)
            );

            input.type = INPUT_KEYBOARD;
            input.ki.wScan = SCAN_A;
            input.ki.dwFlags =
                KEYEVENTF_SCANCODE |
                KEYEVENTF_KEYUP;

            SendInput(
                1,
                &input,
                sizeof(INPUT)
            );
        }

    }
    else if (g_script_dir == DIR_D) {

        if (!g_physical_d) {

            INPUT input;
            ZeroMemory(
                &input,
                sizeof(input)
            );

            input.type = INPUT_KEYBOARD;
            input.ki.wScan = SCAN_D;
            input.ki.dwFlags =
                KEYEVENTF_SCANCODE |
                KEYEVENTF_KEYUP;

            SendInput(
                1,
                &input,
                sizeof(INPUT)
            );
        }
    }

    g_script_dir = DIR_NONE;

    if (g_hwnd) {

        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }

    printf("\nExiting.\n");

    return 0;
}