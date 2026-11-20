// jni/ModMenu/MemoryHelper.h
#pragma once

#include "Dobby/include/dobby.h"
#include "Includes/Logger.h"
#include "Includes/Utils.h"
#include "KittyMemory/KittyMemory.h"
#include "KittyMemory/MemoryPatch.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>
#include <unordered_map>

namespace ModMemory {

// Waits until a shared library is mapped into /proc/self/maps and returns its base address (0 on timeout).
inline uintptr_t WaitForLibrary(const char *libName, int timeoutSeconds = 60) {
    if (!libName || !*libName) return 0;
    const int ticks = timeoutSeconds * 10;
    for (int i = 0; i < ticks; ++i) {
        if (isLibraryLoaded(libName)) {
            uintptr_t base = static_cast<uintptr_t>(findLibrary(libName));
            if (base != 0) return base;
        }
        usleep(100000);
    }
    return 0;
}

inline uintptr_t LibraryBase(const char *libName) {
    if (!libName || !*libName) return 0;
    return static_cast<uintptr_t>(findLibrary(libName));
}

// Crash-safe in-process memory read via kernel copy_from_user (pipe2) — never segfaults on bad pointers.
inline bool SafeRead(uintptr_t address, void *out, size_t size) {
    if (size == 0) return true;
    if (!out) return false;
#if defined(__aarch64__)
    const uintptr_t untagged = address & 0x00FFFFFFFFFFFFFFULL;
#else
    const uintptr_t untagged = address;
#endif
    if (untagged < 0x10000 || untagged > UINTPTR_MAX - size) return false;

    int fds[2]{-1, -1};
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) return false;
    auto *dst = static_cast<uint8_t *>(out);
    size_t done = 0;
    bool ok = true;
    while (done < size) {
        size_t chunk = (size - done > 4096) ? 4096 : (size - done);
        ssize_t w = -1;
        do { w = write(fds[1], reinterpret_cast<const void *>(address + done), chunk); }
        while (w < 0 && errno == EINTR);
        if (w <= 0) { ok = false; break; }
        size_t got = 0;
        while (got < static_cast<size_t>(w)) {
            ssize_t r = -1;
            do { r = read(fds[0], dst + done + got, static_cast<size_t>(w) - got); }
            while (r < 0 && errno == EINTR);
            if (r <= 0) { ok = false; break; }
            got += static_cast<size_t>(r);
        }
        if (!ok || static_cast<size_t>(w) != chunk) { ok = false; break; }
        done += chunk;
    }
    close(fds[0]);
    close(fds[1]);
    return ok;
}

// Managed KittyMemory hex patch registry (toggle ON to patch, OFF to restore original bytes).
class PatchRegistry {
public:
    static PatchRegistry &Instance() {
        static auto *inst = new PatchRegistry;
        return *inst;
    }

    bool SetHexPatch(const std::string &id, const std::string &libName, uintptr_t offset,
                     const std::string &hexBytes, bool enable, std::string &outMsg) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = patches_.find(id);
        if (it == patches_.end()) {
            MemoryPatch p = MemoryPatch::createWithHex(libName.c_str(), offset, hexBytes);
            if (!p.isValid()) {
                outMsg = "Patch invalid or library not loaded (" + libName + ").";
                return false;
            }
            it = patches_.emplace(id, std::move(p)).first;
        }
        if (enable) {
            if (it->second.Modify()) {
                outMsg = "Patched " + libName + " + 0x" + ToHex(offset);
                return true;
            }
            outMsg = "MemoryPatch::Modify failed at " + libName + " + 0x" + ToHex(offset);
            return false;
        } else {
            if (it->second.Restore()) {
                outMsg = "Restored " + libName + " + 0x" + ToHex(offset);
                return true;
            }
            outMsg = "MemoryPatch::Restore failed.";
            return false;
        }
    }

    void RestoreAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &[id, patch] : patches_) {
            if (patch.isValid()) patch.Restore();
        }
    }

private:
    static std::string ToHex(uintptr_t v) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
        return buf;
    }
    std::mutex mutex_;
    std::unordered_map<std::string, MemoryPatch> patches_;
};

// Dobby inline hook helper by absolute address or library+offset.
inline bool HookAddress(uintptr_t address, void *replacement, void **outOriginal) {
    if (!address || !replacement) return false;
    return DobbyHook(reinterpret_cast<void *>(address), replacement, outOriginal) == 0;
}

inline bool HookOffset(const char *libName, uintptr_t offset, void *replacement, void **outOriginal) {
    uintptr_t base = LibraryBase(libName);
    if (!base) return false;
    return HookAddress(base + offset, replacement, outOriginal);
}

inline bool HookSymbol(const char *libName, const char *symbolName, void *replacement, void **outOriginal) {
    void *sym = DobbySymbolResolver(libName, symbolName);
    if (!sym) return false;
    return DobbyHook(sym, replacement, outOriginal) == 0;
}

} // namespace ModMemory

// Convenience macro for clean hook declarations in Features.cpp:
//   MOD_HOOK(float, GetMoveSpeed, void *instance) {
//       float orig = orig_GetMoveSpeed(instance);
//       return gSpeedHack ? orig * gSpeedMult : orig;
//   }
#define MOD_HOOK(ret, name, ...)           \
    static ret (*orig_##name)(__VA_ARGS__) = nullptr; \
    static ret hook_##name(__VA_ARGS__)
