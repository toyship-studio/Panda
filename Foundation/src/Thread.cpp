#include "Foundation/PlatformDetection.hpp"
#include "Foundation/Thread.hpp"

#ifdef PLATFORM_POSIX
#    include <pthread.h>
#endif
#if defined(PLATFORM_WINDOWS)
#    include <windows.h>
#    include <cstdlib>
#    include <cstring>
#    include <malloc.h>
#endif

namespace Foundation {

void setCurrentThreadName(const char *name) {
#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
    pthread_setname_np(name);
#elif defined(PLATFORM_LINUX)
    pthread_setname_np(pthread_self(), name);
#elif defined(PLATFORM_WINDOWS)
    // SetThreadDescription — Win10 Creators Update+; resolved dynamically for the sake of old
    // systems.
    typedef HRESULT(WINAPI * SetThreadDescriptionProc)(HANDLE, PCWSTR);
    SetThreadDescriptionProc SetThreadDescription =
        (SetThreadDescriptionProc)::GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetThreadDescription");
    if (SetThreadDescription != NULL) {
        const size_t length = strlen(name) + 1;
        wchar_t *wide = (wchar_t *)_alloca(length * sizeof(wchar_t));
        mbstowcs(wide, name, length);
        SetThreadDescription(GetCurrentThread(), wide);
    }
#    if defined(_MSC_VER)
    // Legacy path for the VS debugger (exception 0x406d1388).
#        pragma pack(push, 8)
    struct ThreadName {
        DWORD type;
        LPCSTR name;
        DWORD id;
        DWORD flags;
    };
#        pragma pack(pop)
    ThreadName tn;
    tn.type = 0x1000;
    tn.name = name;
    tn.id = ::GetCurrentThreadId();
    tn.flags = 0;

    __try {
        RaiseException(0x406d1388, 0, sizeof(tn) / 4, reinterpret_cast<ULONG_PTR *>(&tn));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
#    endif // _MSC_VER
#else
    (void)name; // TODO: Android (prctl PR_SET_NAME)
#endif // PLATFORM_
}

} // namespace Foundation
