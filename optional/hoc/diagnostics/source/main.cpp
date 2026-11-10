// SPDX-License-Identifier: GPL-2.0-only
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "nexus_hoc_protocol.hpp"

namespace {
const char *Phase(u32 phase) {
    switch (phase) {
        case nexus_hoc::Empty: return "not prepared";
        case nexus_hoc::Prepared: return "prepared; NOT committed";
        case nexus_hoc::Committed: return "committed";
        case nexus_hoc::Failed: return "FAILED";
        default: return "invalid";
    }
}
void Hex(char *out, const u8 *input, size_t count) {
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        out[2 * i] = digits[input[i] >> 4]; out[2 * i + 1] = digits[input[i] & 15];
    }
    out[2 * count] = 0;
}
bool Save(const char *report, char *path, size_t capacity) {
    mkdir("sdmc:/switch/nexus-hoc-split", 0777);
    mkdir("sdmc:/switch/nexus-hoc-split/reports", 0777);
    snprintf(path, capacity, "sdmc:/switch/nexus-hoc-split/reports/%016llx.txt",
        static_cast<unsigned long long>(svcGetSystemTick()));
    const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return false;
    size_t offset = 0, length = strlen(report);
    bool okay = true;
    while (offset < length) {
        const ssize_t wrote = write(fd, report + offset, length - offset);
        if (wrote <= 0) { okay = false; break; }
        offset += static_cast<size_t>(wrote);
    }
    if (okay && fsync(fd) != 0) okay = false;
    if (close(fd) != 0) okay = false;
    return okay;
}
}

int main(int, char **) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    char report[2300]{}, saved_path[256]{};
    bool refresh = true;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if (pressed & HidNpadButton_X) refresh = true;
        if (refresh) {
            refresh = false;
            nexus_hoc::Status status{};
            Handle handle = INVALID_HANDLE;
            Result rc = svcConnectToNamedPort(&handle, nexus_hoc::ServiceName);
            if (R_SUCCEEDED(rc)) {
                Service helper{};
                serviceCreate(&helper, handle);
                rc = serviceDispatchOut(&helper, nexus_hoc::StatusCommand, status);
                serviceClose(&helper);
            }
            u8 usb_present = 0;
            const SmServiceName usb_name = smEncodeName("ams:usb");
            // Atmosphere's nonblocking HasService extension, matching the
            // pinned firmware's sm_ams.os.horizon.c (no deferred GetService).
            const Result usb_rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, usb_name, usb_present);
            const bool usb_registered = (usb_present & 1) != 0;
            const u32 firmware = (hosversionGet() & 0x00FFFFFFU) << 8;
            const bool valid = R_SUCCEEDED(rc) && nexus_hoc::ValidDescriptor(status.descriptor, firmware)
                && status.reserved == 0 && status.pcv_phase <= nexus_hoc::Failed && status.ptm_phase <= nexus_hoc::Failed;
            char settings[65]{}, pcv_id[65]{}, ptm_id[65]{};
            Hex(settings, status.descriptor.settings_hash, 32);
            Hex(pcv_id, status.pcv_module_id, 32);
            Hex(ptm_id, status.ptm_module_id, 32);
            snprintf(report, sizeof(report),
                "Nexus HOC Status HS2 0.2.0\n\n"
                "Helper query: %08x; valid protocol: %s\n"
                "HOC engine: %u; ABI: %u; firmware: %08x\n"
                "PCV: %s (prepared %u, committed %u)\n"
                "PTM: %s (prepared %u, committed %u)\n"
                "Last helper error: %08x\n"
                "USB service registered: %s (query %08x)\n\n"
                "Settings SHA256:\n%s\n"
                "PCV module ID:\n%s\n"
                "PTM module ID:\n%s\n\n"
                "This checks boot status, not overclock stability or USB I/O.\n"
                "Next: check HOC controls, one SD launch and one USB launch.\n",
                rc, valid ? "yes" : "NO", status.descriptor.engine_version, status.descriptor.abi,
                status.descriptor.firmware,
                valid ? Phase(status.pcv_phase) : "unknown", status.pcv_prepares, status.pcv_commits,
                valid ? Phase(status.ptm_phase) : "unknown", status.ptm_prepares, status.ptm_commits,
                status.last_error, R_SUCCEEDED(usb_rc) ? (usb_registered ? "yes" : "no") : "unknown", usb_rc,
                settings, pcv_id, ptm_id);
            const bool saved = Save(report, saved_path, sizeof(saved_path));
            consoleClear();
            printf("%s\nReport %s: %s\n\nX refresh/save   + exit\n", report, saved ? "saved" : "SAVE FAILED", saved_path);
            if (!valid) printf("\nHOC is optional: no helper is expected on USB-only boot.\nUse the USB + HOC entry to test HOC. Query errors are saved too.\n");
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 0;
}
