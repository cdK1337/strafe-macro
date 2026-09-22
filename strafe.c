#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <mmsystem.h>

#define SCAN_A    0x1E
#define SCAN_D    0x20
#define MOUSE_5   VK_XBUTTON1
#define SMOOTH    8

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

static void move_mouse(int dx) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(INPUT));
}

static int is_console_active(void) {
    return GetForegroundWindow() == GetConsoleWindow();
}

static void show_status(int rate, int pixels, int start_a) {
    printf("\rRate: %3d/s  Pixels: %3d  Start: [%s]   ", rate, pixels, start_a ? "A" : "D");
    fflush(stdout);
}

int main(void) {
    int rate = 12;
    int pixels = 10;
    int start_a = 1;
    int ms_per_half;
    int step_px;
    int step_ms;
    char buf[32];

    timeBeginPeriod(1);

    printf("Strafe macro\n");
    printf("============\n\n");

    printf("Strafes per second [12]: ");
    if (fgets(buf, sizeof(buf), stdin)) {
        int v = atoi(buf);
        if (v > 0) rate = v;
    }

    printf("Pixels per move [10]: ");
    if (fgets(buf, sizeof(buf), stdin)) {
        int v = atoi(buf);
        if (v > 0) pixels = v;
    }

    printf("Start with (A/D) [A]: ");
    if (fgets(buf, sizeof(buf), stdin)) {
        if (buf[0] == 'd' || buf[0] == 'D')
            start_a = 0;
    }

    ms_per_half = 500 / rate;
    if (ms_per_half < 1) ms_per_half = 1;

    printf("\n");
    show_status(rate, pixels, start_a);
    printf("\nHold mouse5 to strafe\n");
    printf("Console keys: Up/Down=rate  Left/Right=pixels  Space=A/D  Esc=exit\n");

    for (;;) {
        if (is_console_active()) {
            if (GetAsyncKeyState(VK_UP) & 1) {
                rate++;
                ms_per_half = 500 / rate;
                if (ms_per_half < 1) ms_per_half = 1;
                show_status(rate, pixels, start_a);
            }
            if (GetAsyncKeyState(VK_DOWN) & 1) {
                if (rate > 1) rate--;
                ms_per_half = 500 / rate;
                show_status(rate, pixels, start_a);
            }
            if (GetAsyncKeyState(VK_RIGHT) & 1) {
                pixels++;
                show_status(rate, pixels, start_a);
            }
            if (GetAsyncKeyState(VK_LEFT) & 1) {
                if (pixels > 1) pixels--;
                show_status(rate, pixels, start_a);
            }
            if (GetAsyncKeyState(VK_SPACE) & 1) {
                start_a = !start_a;
                show_status(rate, pixels, start_a);
            }
            if (GetAsyncKeyState(VK_ESCAPE) & 1) {
                timeEndPeriod(1);
                printf("\n");
                return 0;
            }
        }

        if (!(GetAsyncKeyState(MOUSE_5) & 0x8000)) {
            Sleep(1);
            continue;
        }

        step_px = pixels / SMOOTH;
        if (step_px < 1) step_px = 1;
        step_ms = ms_per_half / SMOOTH;
        if (step_ms < 1) step_ms = 1;

        if (start_a) {
            key_press(SCAN_A);
            for (int i = 0; i < SMOOTH; i++) {
                if (!(GetAsyncKeyState(MOUSE_5) & 0x8000)) break;
                move_mouse(-step_px);
                Sleep(step_ms);
            }
            key_release(SCAN_A);

            key_press(SCAN_D);
            for (int i = 0; i < SMOOTH; i++) {
                if (!(GetAsyncKeyState(MOUSE_5) & 0x8000)) break;
                move_mouse(step_px);
                Sleep(step_ms);
            }
            key_release(SCAN_D);
        } else {
            key_press(SCAN_D);
            for (int i = 0; i < SMOOTH; i++) {
                if (!(GetAsyncKeyState(MOUSE_5) & 0x8000)) break;
                move_mouse(step_px);
                Sleep(step_ms);
            }
            key_release(SCAN_D);

            key_press(SCAN_A);
            for (int i = 0; i < SMOOTH; i++) {
                if (!(GetAsyncKeyState(MOUSE_5) & 0x8000)) break;
                move_mouse(-step_px);
                Sleep(step_ms);
            }
            key_release(SCAN_A);
        }
    }

    return 0;
}
