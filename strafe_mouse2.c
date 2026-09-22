#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define SCAN_A      0x1E
#define SCAN_D      0x20
#define VKEY_A      0x41
#define VKEY_D      0x44

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

static void switch_dir(BYTE new_down, BYTE old_up) {
    INPUT ins[2] = {0};
    ins[0].type = INPUT_KEYBOARD;
    ins[0].ki.wScan = old_up;
    ins[0].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    ins[1].type = INPUT_KEYBOARD;
    ins[1].ki.wScan = new_down;
    ins[1].ki.dwFlags = KEYEVENTF_SCANCODE;
    SendInput(2, ins, sizeof(INPUT));
}

static void ensure_direction(int left) {
    if (left) {
        if (holding_d)
            switch_dir(SCAN_A, SCAN_D);
        else if (!holding_a)
            key_press(SCAN_A);
        holding_a = 1;
        holding_d = 0;
    } else {
        if (holding_a)
            switch_dir(SCAN_D, SCAN_A);
        else if (!holding_d)
            key_press(SCAN_D);
        holding_a = 0;
        holding_d = 1;
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

                            int space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
                            int user_a = (GetAsyncKeyState(VKEY_A) & 0x8000) != 0;
                            int user_d = (GetAsyncKeyState(VKEY_D) & 0x8000) != 0;

                            if (space && dx != 0) {
                                if (user_a || user_d) {
                                    if (holding_a || holding_d) {
                                        release_all();
                                        printf("\rPriority: user key - macro off                  \n");
                                    }
                                } else if (dx < 0) {
                                    ensure_direction(1);
                                } else {
                                    ensure_direction(0);
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
    wc.lpszClassName = L"AutoStrafeWnd2";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AutoStrafe2",
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