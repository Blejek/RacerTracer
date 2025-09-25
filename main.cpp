#include <windows.h>
#include <dinput.h>
#include "assert.h"

#include <iostream>
#include <vector>
#include <functional>
#include <map>
#include <random>

#include <locale>
#include <codecvt>
#include <string>
#include <format>

#include <thread>
#include <chrono>

template<typename... T>
void print(std::string_view format, T&& ...args) {
    std::cout << std::vformat(format, std::make_format_args(args...));
}

void print(std::string_view literal) {
    std::cout << literal;
}

template<typename... T>
void println(std::string_view format, T&& ...args) {
    std::cout << std::vformat(format, std::make_format_args(args...)) << '\n';
}

void println(std::string_view literal) {
    std::cout << literal << '\n';
}

bool g_running = true;
LPDIRECTINPUT8 g_pDI = nullptr;
LPDIRECTINPUTDEVICE8 g_pWheel = nullptr;
LPDIRECTINPUTDEVICE8  g_pKeyboard = nullptr;
std::string g_wheelName("");
double g_throttle = 0.0;
std::string_view g_throttleColor{ "48;2;111;166;122" };
double g_brake = 0.0;
std::string_view g_brakeColor{ "48;2;168;50;71" };

bool g_pracBrake = true;

// in n%
int g_bias = 5;
int g_biasMax = 100;
int g_biasMin = 1;

// in n*10ms
int g_time = 100;
int g_timeMax = 1000;
int g_timeMin = 10;

using keyFn = std::function<void()>;
using key = size_t;
using keyFnMap = std::map<key, keyFn>;

keyFnMap g_keyCallbacks;

bool InitDirectInput(HINSTANCE instance) {
    return SUCCEEDED(
        DirectInput8Create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr)
    );
}

BOOL CALLBACK EnumDevicesCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    std::wstring deviceName = pdidInstance->tszProductName;
    int nameSize = WideCharToMultiByte(CP_UTF8, 0, pdidInstance->tszProductName, -1, nullptr, 0, nullptr, nullptr);
    char* nameStr = new char[nameSize];

    if (nameStr) {
        WideCharToMultiByte(CP_UTF8, 0, pdidInstance->tszProductName, -1, nameStr, nameSize, nullptr, nullptr);
        g_wheelName = nameStr;
        delete[] nameStr;
    }

    println(" - {}", g_wheelName);

    if (FAILED(g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pWheel, nullptr)) ||
        FAILED(g_pWheel->SetDataFormat(&c_dfDIJoystick)) ||
        FAILED(g_pWheel->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)) ||
        FAILED(g_pWheel->Acquire()))
        return DIENUM_CONTINUE;

    println("Using wheel: {}", g_wheelName);
    return DIENUM_STOP;
}

bool SetupDevice() {
    println("Searching for wheel:");

    // get wheel

    if (!SUCCEEDED(
        g_pDI->EnumDevices(
            DI8DEVCLASS_GAMECTRL,
            EnumDevicesCallback,
            nullptr,
            DIEDFL_ATTACHEDONLY)
    )) return false;

    // get keyboard

    if (FAILED(g_pDI->CreateDevice(GUID_SysKeyboard, &g_pKeyboard, nullptr)) ||
        FAILED(g_pKeyboard->SetDataFormat(&c_dfDIKeyboard)) ||
        FAILED(g_pKeyboard->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)) ||
        FAILED(g_pKeyboard->Acquire())) {
        return false;
    }

    return true;
}

void PollWheel() {
    if (!g_pWheel) 
        return;

    DIJOYSTATE js;
    HRESULT hr = g_pWheel->Poll();
    if (FAILED(hr)) {
        g_pWheel->Acquire();
        return;
    }

    if (FAILED(g_pWheel->GetDeviceState(sizeof(DIJOYSTATE), &js)))
        return;

    g_throttle = js.lY;
    g_brake = js.lRz;
}

bool PollKeyboard() {
    if (!g_pKeyboard)
        return false;

    HRESULT hr = g_pKeyboard->Poll();
    if (FAILED(hr)) {
        g_pKeyboard->Acquire();
        return false;
    }

    BYTE keyState[256];
    hr = g_pKeyboard->GetDeviceState(sizeof(keyState), (LPVOID)keyState);
    if (FAILED(hr)) {
        return false;
    }

    for (key i = 0; i < 256; i++) {
        if (keyState[i] & 0x80 && g_keyCallbacks.find(i) != g_keyCallbacks.end()) {
            g_keyCallbacks.at(i)();
        }
    }
}

void Cleanup() {
    if (g_pWheel) {
        g_pWheel->Unacquire();
        g_pWheel->Release();
        g_pWheel = nullptr;
    }

    if (g_pKeyboard) {
        g_pKeyboard->Unacquire();
        g_pKeyboard->Release();
        g_pKeyboard = nullptr;
    }

    if (g_pDI) {
        g_pDI->Release();
        g_pDI = nullptr;
    }
}

void SetColor(std::string_view ansi) {
    print("\033[{}m", ansi);

}
void ResetColor() {
    print("\033[0m");
}

void ResetCursor(HANDLE console) {
    SetConsoleCursorPosition(console, { 0, 0 });
}

void ClearConsole(HANDLE console) {
    ResetColor();
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;

    if (console == INVALID_HANDLE_VALUE) return;

    if (!GetConsoleScreenBufferInfo(console, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;

    FillConsoleOutputCharacter(console, (TCHAR)' ', cellCount, { 0, 0 }, &count);
    FillConsoleOutputAttribute(console, csbi.wAttributes, cellCount, { 0, 0 }, &count);
    ResetCursor(console);
}

void DrawBar(std::string_view color, double x, int cSize) {
    ResetColor();
    print("\n    ");
    if (x < 0.1) 
        print("  ");
    else if (x < 1.0) 
        print(" ");
    print("{:03.1f}% ", x * 100.0);

    SetColor(color);

    int filled = x * cSize;
    for(int i = 0; i < cSize; i++){
        print(" ");
        if (i >= filled) {
            SetColor("48;2;219;220;227");
        }
    }
    ResetColor();
    println("");
}

void DrawUI(double rndVal) {
    println("\n   Wheel: {}   ", g_wheelName);
    println("\n   Bias: {}% ( \u2190 \u2192 )      ", g_bias);
    println("   Hold time: {}s ( \u2191 \u2193 )      ", g_time / 100.0);
    ResetColor();
    print("\n   You must hit ");
    SetColor(g_pracBrake ? g_brakeColor : g_throttleColor);
    print("{:03.1f}%", rndVal * 100.0);
    ResetColor();
    print(" ( tab )                \n");

    // bars
    DrawBar(g_throttleColor, (65536 - g_throttle) / 65536, 80);
    DrawBar(g_brakeColor, (65536 - g_brake) / 65536, 80);
}

double RandomBarValue() {
    static std::random_device s_rd;
    static std::mt19937 s_gen(s_rd());
    static std::uniform_real_distribution<> s_dis(0.0, 1.0);
    return s_dis(s_gen);
}

int main() {
    using namespace std::chrono_literals;
    if (!InitDirectInput(GetModuleHandle(nullptr))) {
        std::cerr << "Failed to initialize DirectInput." << std::endl;
        return 1;
    }

    if (!SetupDevice()) {
        std::cerr << "Failed to set up the G29 wheel." << std::endl;
        Cleanup();
        return 1;
    }

    // set callbacks

    // TODO: change
    g_keyCallbacks[DIK_ESCAPE] = []() {
        g_running = false;
    };
    g_keyCallbacks[DIK_TAB] = []() {
        g_pracBrake = !g_pracBrake;
        std::this_thread::sleep_for(64ms);
    };
    g_keyCallbacks[DIK_UPARROW] = []() {
        if(g_time < g_timeMax)
            g_time += 1;
        std::this_thread::sleep_for(64ms);
    };
    g_keyCallbacks[DIK_DOWNARROW] = []() {
        if (g_time > g_timeMin)
            g_time -= 1;
        std::this_thread::sleep_for(64ms);
    };
    g_keyCallbacks[DIK_LEFTARROW] = []() {
        if (g_bias < g_biasMax)
            g_bias += 1;
        std::this_thread::sleep_for(64ms);
    };
    g_keyCallbacks[DIK_RIGHTARROW] = []() {
        if (g_bias > g_biasMin)
            g_bias -= 1;
        std::this_thread::sleep_for(64ms);
    };

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);    
    DWORD dwMode = 0;
    GetConsoleMode(hConsole, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hConsole, dwMode);
    ClearConsole(hConsole);

    double rndVal = RandomBarValue();

    auto prev = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> dur{ now - prev };
    std::chrono::duration<double> ts{ now - prev };

    while (g_running) {
        ResetCursor(hConsole);

        PollWheel();
        PollKeyboard();

        double val = (65536.0 - (g_pracBrake ? g_brake : g_throttle)) / 65536.0;
        if (val < rndVal + g_bias / 200.0 && 
            val > rndVal - g_bias / 200.0) {

            if (ts.count() > g_time / 100.0) {
                rndVal = RandomBarValue();
                ts = dur;
            }
            else {
                ts += dur;
            }
        }
        now = std::chrono::steady_clock::now();
        dur = now - prev;
        prev = now;

        DrawUI(rndVal);

        std::this_thread::sleep_for(16ms);
    }

    ClearConsole(hConsole);
    Cleanup();
    return 0;
}

