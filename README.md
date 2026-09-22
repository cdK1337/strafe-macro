# strafe-macro

Автоматический стрейф макрос для CS 1.6 на Windows (raw input + scan codes).

## Описание

Макросы отслеживают движение мыши через **raw input** (`RegisterRawInputDevices`) и
нажимают клавиши **A/D** через scan codes (`KEYEVENTF_SCANCODE`), которые корректно
воспринимаются CS 1.6 (в отличие от `keybd_event`/virtual keys, которые игра игнорирует).

## Программы

| Файл | Описание |
|------|----------|
| `strafe.exe` | Автоматический стрейф влево-вправо (A/D + движение мыши), запускается по mouse5. Скорость/пиксели настраиваются в консоли |
| `strafe_mouse.exe` | Держи **SPACE** + двигай мышь: влево → зажата A, вправо → зажата D |
| `strafe_mouse2.exe` | Как `strafe_mouse`, но с приоритетом твоих физических нажатий A/D |
| `strafe_mouse3.exe` | Как `strafe_mouse`, но A/D **удерживаются** (латун) даже после остановки мыши |
| `strafe_mouse4.exe` | Как `strafe_mouse` + приоритет физических A/D (сырой ввод клавиатуры) |
| `strafe_mouse5.exe` | Атомарный `SendInput(2)` при смене направления, idle-timeout через QPC, `MsgWaitForMultipleObjectsEx` |
| `strafe_mouse6.exe` | Физические A/D через `WH_KEYBOARD_LL` хук с фильтром `LLKHF_INJECTED` (свои инжекты не считаются). Плюс физические W/S имеют приоритет |

## Сборка

```bash
# ftp-образный пример с MinGW-w64
gcc -O2 -o strafe.exe strafe.c -luser32 -lwinmm
gcc -O2 -o strafe_mouse.exe strafe_mouse.c -luser32
# остальные без -lwinmm
```

## Примечание

Программы используют стандартные Windows API и не модифицируют память игры.
Анти-чит (VAC) такие external-макросы не детектирует, но поведение может
отслеживаться статистически. Используй на свой страх и риск.