#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define SCAN_A    0x1E
#define SCAN_D    0x20
#define VKEY_A    0x41
#define VKEY_D    0x44

static int holding_a = 0;
static int holding_d = 0;
static int physical_a = 0;
static int physical_d = 0;
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
                    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                        int down = !(raw->data.keyboard.Flags & RI_KEY_BREAK);
                        if (raw->data.keyboard.VKey == VKEY_A)
                            physical_a = down;
                        else if (raw->data.keyboard.VKey == VKEY_D)
                            physical_d = down;
                    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
                        if (raw->data.mouse.usFlags == MOUSE_MOVE_RELATIVE) {
                            int dx = raw->data.mouse.lLastX;
                            last_move_tick = GetTickCount();

                            int space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

                            if (dx != 0 && space) {
                                if (physical_a || physical_d) {
                                    if (holding_a || holding_d) {
                                        release_all();
                                        printf("\rPriority: user key - macro off                  \n");
                                    }
                                } else if (dx < 0) {
                                    set_key(SCAN_A, SCAN_D, &holding_a, &holding_d);
                                } else {
                                    set_key(SCAN_D, SCAN_A, &holding_d, &holding_a);
                                }
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
    printf("Auto-strafe by raw mouse input (user priority)\n");
    printf("==============================================\n\n");
    printf("Hold SPACE + move mouse: left -> A, right -> D\n");
    printf("If YOU press/hold A or D - manual control, macro backs off\n");
    printf("Close console to exit.\n\n");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"AutoStrafeWnd4";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AutoStrafe4",
                                WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
                                NULL, NULL, wc.hInstance, NULL);

    RAWINPUTDEVICE rids[2] = {0};
    rids[0].usUsagePage = 0x01;
    rids[0].usUsage = 0x02;
    rids[0].dwFlags = RIDEV_INPUTSINK;
    rids[0].hwndTarget = hwnd;
    rids[1].usUsagePage = 0x01;
    rids[1].usUsage = 0x06;
    rids[1].dwFlags = RIDEV_INPUTSINK;
    rids[1].hwndTarget = hwnd;

    if (!RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE))) {
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