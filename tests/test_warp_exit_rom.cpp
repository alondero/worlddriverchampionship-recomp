#undef NDEBUG
#include "game_warp.h"
#include "recomp.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#include <windows.h>
#endif

extern "C" {
void wdc_warp_menu(uint8_t*, recomp_context*);
void wdc_warp_load(uint8_t*, recomp_context*);
void func_8004E950(uint8_t*, recomp_context*);
void func_80058650(uint8_t*, recomp_context*);
void func_8006F63C(uint8_t*, recomp_context*) {}
void wdc_cheat_car_available(uint8_t*, recomp_context*) {}
void do_break(uint32_t) { std::abort(); }
void switch_error(const char*, uint32_t, uint32_t) { std::abort(); }
}

namespace {
constexpr int32_t career = int32_t(0x80097D00);
constexpr int32_t menu_object = int32_t(0x800952E8);
constexpr int32_t menu_state = int32_t(0x80099828); // lw -0x67D8(0x800A0000), not 0x800A9828
constexpr int32_t selected_record = int32_t(0x800DF930);

void load_rom_image(uint8_t* rdram, const char* path) {
    std::ifstream rom(path, std::ios::binary);
    assert(rom);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom.gcount() == std::streamsize(data.size()));
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];
}

recomp_context context() {
    recomp_context ctx{};
    ctx.r29 = int32_t(0x807FF000);
    ctx.f_odd = &ctx.f0.u32h;
    return ctx;
}

void initialise_career(uint8_t* rdram) {
    (void)rdram;
    for (int i = 0; i < 35; ++i) {
        MEM_W(0xB4C + i * 20 + 4, career) = 0;
        MEM_W(0xB4C + i * 20 + 8, career) = i;
    }
    MEM_W(0x1068, career) = 0;
    MEM_W(0x106C, career) = 0;
    MEM_W(0x1070, career) = 0;
    // Native career setup groups the 35 model records into fifteen 40-byte
    // class lists. The class metadata itself is ROM data already loaded above.
    for (int i = 0; i < 35; ++i) {
        const int car_class = MEM_W(0, int32_t(0x800810A8 + i * 0x80));
        const int32_t list = career + 0x2C + car_class * 40;
        const int count = MEM_W(0x14, list);
        MEM_W(0x04 + count * 4, list) = career + 0xB4C + i * 20;
        MEM_W(0x14, list) = count + 1;
    }
}

void prepare_menu_frame(uint8_t* rdram) {
    MEM_W(0, int32_t(0x8007FE14)) = 1; // presentation ready; no wait loop
    MEM_W(0, int32_t(0x80092EB8)) = 1; // menu assets already loaded
    MEM_B(0, int32_t(0x80092EBC)) = 1; // menu remains active
    MEM_W(0, menu_state) = 36;         // Quick Race resume state in the crash chain
    MEM_W(0x17E4, menu_object + 0x128) = 4; // native constructor's populated subobject state
}

void run_menu_chain(uint8_t* rdram) {
    auto ctx = context();
    func_8004E950(rdram, &ctx);
}

void warped_exit(uint8_t* rdram) {
    initialise_career(rdram);
    auto ctx = context();
    MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8004E950);
    MEM_B(0, int32_t(0x80092EBC)) = 1;
    assert(wdc::warp::request({1, 0, wdc::warp::Mode::Arcade, 3}));
    wdc_warp_menu(rdram, &ctx);
    wdc_warp_load(rdram, &ctx);
    prepare_menu_frame(rdram);
    run_menu_chain(rdram);
}

void null_control(uint8_t* rdram) {
    initialise_career(rdram);
    prepare_menu_frame(rdram);
    run_menu_chain(rdram);
}

void native_control(uint8_t* rdram) {
    initialise_career(rdram);
    MEM_W(0, menu_state) = 37; // native setup state that publishes the selection
    auto ctx = context();
    ctx.r4 = menu_object + 0x128;
    MEM_B(0x17ED, menu_object + 0x128) = 0;
    func_80058650(rdram, &ctx);
    prepare_menu_frame(rdram);
    run_menu_chain(rdram);
}

int child(const std::string& scenario, const char* rom_path) {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    load_rom_image(rdram, rom_path);
    if (scenario == "warped-exit") warped_exit(rdram);
    else if (scenario == "null-control") null_control(rdram);
    else if (scenario == "native-control") native_control(rdram);
    else return 2;
    std::cout << scenario << " completed\n";
    return 0;
}

int spawn_child(const char* exe, const std::string& scenario, const char* rom_path) {
#ifdef _WIN32
    std::string command = "\"" + std::string(exe) + "\" " + scenario + " \"" + rom_path + "\"";
    STARTUPINFOA startup{};
    PROCESS_INFORMATION process{};
    startup.cb = sizeof(startup);
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 2;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 2;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return int(code);
#else
    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        execl(exe, exe, scenario.c_str(), rom_path, static_cast<char*>(nullptr));
        _exit(2);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128;
#endif
}
}

int main(int argc, char** argv) {
    if (argc == 3) return child(argv[1], argv[2]);
    assert(argc == 2);
    const int warped = spawn_child(argv[0], "warped-exit", argv[1]);
    const int native = spawn_child(argv[0], "native-control", argv[1]);
    const int null_control_result = spawn_child(argv[0], "null-control", argv[1]);
    std::cout << "warped=" << warped << " native=" << native << " null-control=" << null_control_result << "\n";
    // The warped lifecycle and native control must complete. The negative control
    // must still reach the missing-record failure rather than becoming a silent success.
    return warped == 0 && native == 0 && null_control_result != 0 ? 0 : 1;
}
