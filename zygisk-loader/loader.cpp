// zygisk-loader/loader.cpp
#include <sys/types.h>
#include <android/dlext.h>
#include <android/log.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <jni.h>
#include <memory>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>
#include "zygisk.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ModMenuLoader", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ModMenuLoader", __VA_ARGS__)

namespace {
constexpr size_t kMaxPayload = 32 * 1024 * 1024;
constexpr unsigned kDefaultDelaySeconds = 3;
constexpr unsigned kMaxDelaySeconds = 180;
constexpr const char *kPayloadEntryName = "payload/arm64-v8a.so";

unsigned parseDelaySeconds(const std::vector<unsigned char> &bytes, unsigned fallback) {
    size_t i = 0;
    while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r' || bytes[i] == '\n')) ++i;
    if (i >= bytes.size() || bytes[i] < '0' || bytes[i] > '9') return fallback;
    unsigned long long val = 0;
    while (i < bytes.size() && bytes[i] >= '0' && bytes[i] <= '9') {
        val = val * 10ULL + static_cast<unsigned long long>(bytes[i] - '0');
        if (val >= kMaxDelaySeconds) return kMaxDelaySeconds;
        ++i;
    }
    return static_cast<unsigned>(val);
}

bool readFile(int dir, const char *name, size_t maximum, std::vector<unsigned char> &out) {
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st{};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 &&
              static_cast<uint64_t>(st.st_size) <= maximum;
    if (ok) {
        out.resize(static_cast<size_t>(st.st_size));
        size_t done = 0;
        while (done < out.size()) {
            ssize_t n = read(fd, out.data() + done, out.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = false; break; }
            done += static_cast<size_t>(n);
        }
    }
    close(fd);
    if (!ok) out.clear();
    return ok;
}

bool allowed(const std::vector<unsigned char> &text, const char *process) {
    std::string input(text.begin(), text.end());
    size_t start = 0;
    while (start < input.size()) {
        size_t end = input.find('\n', start);
        if (end == std::string::npos) end = input.size();
        std::string line = input.substr(start, end - start);
        size_t comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        size_t first = line.find_first_not_of(" \t\r");
        size_t last = line.find_last_not_of(" \t\r");
        if (first != std::string::npos && line.substr(first, last - first + 1) == process)
            return true;
        start = end + 1;
    }
    return false;
}

bool validPayload(const std::vector<unsigned char> &bytes) {
    if (bytes.size() < sizeof(Elf64_Ehdr)) return false;
    Elf64_Ehdr eh{};
    memcpy(&eh, bytes.data(), sizeof(eh));
    return memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
           eh.e_ident[EI_CLASS] == ELFCLASS64 &&
           eh.e_ident[EI_DATA] == ELFDATA2LSB &&
           eh.e_machine == EM_AARCH64 && eh.e_type == ET_DYN;
}

struct Work {
    JavaVM *vm = nullptr;
    std::vector<unsigned char> payload;
    std::string appDataDir;
    unsigned delaySeconds = kDefaultDelaySeconds;
#if defined(__x86_64__) || defined(__i386__)
    using Initialized = bool (*)();
    using LoadExt = void *(*)(const char *, int, void *);
    using Trampoline = void *(*)(void *, const char *, const char *, uint32_t);
    using GetNamespace = void *(*)(const char *);
    using CreateNamespace = void *(*)(const char *, const char *, const char *, uint64_t, const char *, void *);
    using LoadLegacy = void *(*)(const char *, int);
    Initialized initialized = nullptr;
    LoadExt load = nullptr;
    Trampoline trampoline = nullptr;
    GetNamespace getNamespace = nullptr;
    CreateNamespace createNamespace = nullptr;
    LoadLegacy loadLegacy = nullptr;

    bool prepareBridge() {
        void *h = dlopen("/apex/com.android.art/lib64/libnativebridge.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen("libnativebridge.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) return false;
        initialized = reinterpret_cast<Initialized>(dlsym(h, "NativeBridgeInitialized"));
        load = reinterpret_cast<LoadExt>(dlsym(h, "NativeBridgeLoadLibraryExt"));
        trampoline = reinterpret_cast<Trampoline>(dlsym(h, "NativeBridgeGetTrampoline"));
        getNamespace = reinterpret_cast<GetNamespace>(dlsym(h, "NativeBridgeGetExportedNamespace"));
        createNamespace = reinterpret_cast<CreateNamespace>(dlsym(h, "NativeBridgeCreateNamespace"));
        loadLegacy = reinterpret_cast<LoadLegacy>(dlsym(h, "NativeBridgeLoadLibrary"));
        return initialized && load && trampoline;
    }
#endif
};

void *loadWorker(void *raw) {
    std::unique_ptr<Work> work(static_cast<Work *>(raw));
    if (work->delaySeconds > 0) sleep(work->delaySeconds);
#if defined(__x86_64__) || defined(__i386__)
    if (!work->initialized || !work->initialized()) return nullptr;
#endif
    JNIEnv *env = nullptr;
    if (work->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;

#if defined(__x86_64__) || defined(__i386__)
    std::string dir = work->appDataDir + "/files/.mod_cache";
    mkdir((work->appDataDir + "/files").c_str(), 0700);
    mkdir(dir.c_str(), 0700);
    std::string payloadPath = dir + "/libModMenu.so";
    std::string pendingPath = payloadPath + ".pending";
    unlink(pendingPath.c_str());
    int fd = open(pendingPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0700);
#else
    int fd = static_cast<int>(syscall(__NR_memfd_create, "mod-cache", 1));
#endif
    if (fd < 0) { work->vm->DetachCurrentThread(); return nullptr; }
    size_t done = 0;
    while (done < work->payload.size()) {
        ssize_t n = write(fd, work->payload.data() + done, work->payload.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    if (done != work->payload.size() || lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        work->vm->DetachCurrentThread();
        return nullptr;
    }
#if defined(__x86_64__) || defined(__i386__)
    if (fchmod(fd, 0500) < 0 || rename(pendingPath.c_str(), payloadPath.c_str()) < 0) {
        close(fd); work->vm->DetachCurrentThread(); return nullptr;
    }
    void *ns = work->getNamespace ? work->getNamespace("default") : reinterpret_cast<void *>(3);
    if (work->createNamespace) {
        if (void *priv = work->createNamespace("mod_ns", dir.c_str(), dir.c_str(), 2, dir.c_str(), ns))
            ns = priv;
    }
    void *handle = work->load(payloadPath.c_str(), RTLD_NOW, ns);
    if (!handle && work->loadLegacy) handle = work->loadLegacy(payloadPath.c_str(), RTLD_NOW);
#else
    android_dlextinfo ext{};
    ext.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
    ext.library_fd = fd;
    void *handle = android_dlopen_ext("libModMenu.so", RTLD_NOW | RTLD_LOCAL, &ext);
#endif
    close(fd);
    if (handle) {
#if defined(__x86_64__) || defined(__i386__)
        auto onLoad = reinterpret_cast<jint (*)(JavaVM *, void *)>(work->trampoline(handle, "JNI_OnLoad", nullptr, 0));
#else
        auto onLoad = reinterpret_cast<jint (*)(JavaVM *, void *)>(dlsym(handle, "JNI_OnLoad"));
#endif
        if (onLoad) {
            onLoad(work->vm, nullptr);
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        }
    } else {
        LOGE("ModMenu payload load failed");
    }
    work->vm->DetachCurrentThread();
    return nullptr;
}
} // namespace

class ModMenuZygiskLoader final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        selected_ = false;
        if (!args->nice_name || args->uid < 10000 || args->uid % 100000 >= 90000 ||
            (args->is_child_zygote && *args->is_child_zygote)) {
            unload(); return;
        }
        int dir = api_->getModuleDir();
        if (dir < 0) { unload(); return; }
        std::vector<unsigned char> targets;
        if (!readFile(dir, "targets.txt", 65536, targets)) {
            close(dir); unload(); return;
        }
        const char *proc = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!proc) { close(dir); unload(); return; }
        bool match = allowed(targets, proc);
        env_->ReleaseStringUTFChars(args->nice_name, proc);
        if (!match) { close(dir); unload(); return; }

        work_ = std::make_unique<Work>();
        std::vector<unsigned char> delayBytes;
        if (readFile(dir, "delay.txt", 64, delayBytes))
            work_->delaySeconds = parseDelaySeconds(delayBytes, kDefaultDelaySeconds);
        bool ok = env_->GetJavaVM(&work_->vm) == JNI_OK &&
                  readFile(dir, kPayloadEntryName, kMaxPayload, work_->payload) &&
                  validPayload(work_->payload);
        if (ok && args->app_data_dir) {
            const char *d = env_->GetStringUTFChars(args->app_data_dir, nullptr);
            if (d) { work_->appDataDir = d; env_->ReleaseStringUTFChars(args->app_data_dir, d); }
        }
#if defined(__x86_64__) || defined(__i386__)
        if (ok) ok = !work_->appDataDir.empty() && work_->prepareBridge();
#endif
        close(dir);
        if (!ok) { work_.reset(); unload(); return; }
        selected_ = true;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!selected_) return;
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) == 0) {
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_t tid;
            Work *raw = work_.release();
            if (pthread_create(&tid, &attr, loadWorker, raw) != 0) {
                work_.reset(raw);
                unload();
            }
            pthread_attr_destroy(&attr);
        }
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override { unload(); }

private:
    void unload() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool selected_ = false;
    std::unique_ptr<Work> work_;
};

REGISTER_ZYGISK_MODULE(ModMenuZygiskLoader)
