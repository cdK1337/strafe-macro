#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define SCAN_A    0x1E
#define SCAN_D    0x20

static int holding_a = 0;
static int holding_d = 0;
static DWORD last_move_tick = 0;

static void key_press(BYTE scan) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scan;
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    SendInput(1, &input, sizeof(INPUT));
}

static void key_release(BYTE scan) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scan;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

static void set_key(BYTE press_scan, BYTE release_scan, int *press_flag, int *release_flag) {
    if (*release_flag) {
        key_release(release_scan);
        *release_flag = 0;
    }
    if (!*press_flag) {
        key_press(press_scan);
        *press_flag = 1;
    }
    printf("\rOutput: [%s]   ", holding_a ? "A" : holding_d ? "D" : "-");
    fflush(stdout);
}

static void release_all(void) {
    if (holding_a) {
        key_release(SCAN_A);
        holding_a = 0;
    }
    if (holding_d) {
        key_release(SCAN_D);
        holding_d = 0;
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_INPUT) {
        UINT size;
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) == 0 && size > 0) {
            BYTE *buf = (BYTE *)malloc(size);
            if (buf) {
                UINT got = GetRawInputData((HRAWINPUT)l, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER));
                if (got == size) {
                    RAWINPUT *raw = (RAWINPUT *)buf;
                    if (raw->header.dwType == RIM_TYPEMOUSE) {
                        if (raw->data.mouse.usFlags == MOUSE_MOVE_RELATIVE) {
                            int dx = raw->data.mouse.lLastX;
                            last_move_tick = GetTickCount();
                            if (dx != 0 && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                                if (dx < 0)
                                    set_key(SCAN_A, SCAN_D, &holding_a, &holding_d);
                                else
                                    set_key(SCAN_D, SCAN_A, &holding_d, &holding_a);
                            }
                        }
                    }
                }
                free(buf);
            }
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int main(void) {
    printf("Auto-strafe by raw mouse input\n");
    printf("==============================\n\n");
    printf("Hold SPACE: mouse left -> A, mouse right -> D\n");
    printf("Close console to exit.\n\n");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"AutoStrafeWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AutoStrafe",
                                WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
                                NULL, NULL, wc.hInstance, NULL);

    RAWINPUTDEVICE rid = {0};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        printf("Raw input register failed. Error: %lu\n", GetLastError());
        return 1;
    }

    MSG msg;
    for (;;) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.message == WM_QUIT)
            break;

        if (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            if (holding_a || holding_d) {
                release_all();
                printf("\rOutput: [ - ]                            \n");
            }
        } else if (GetTickCount() - last_move_tick > 100) {
            if (holding_a || holding_d) {
                release_all();
                printf("\rIdle: keys released                       \n");
            }
        }

        Sleep(1);
    }

    release_all();
    DestroyWindow(hwnd);
    return 0;
}