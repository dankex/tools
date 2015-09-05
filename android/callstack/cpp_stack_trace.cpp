#include "android/log.h"
#include "cpp_stack_trace.h"

#include <iostream>
#include <sstream>
#include <iomanip>

#include <unwind.h>
#include <dlfcn.h>

#define TAG "CallStack"

namespace {

    struct BacktraceState
    {
        void** current;
        void** end;
    };

    static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg)
    {
        BacktraceState* state = static_cast<BacktraceState*>(arg);
        uintptr_t pc = _Unwind_GetIP(context);
        if (pc) {
            if (state->current == state->end) {
                return _URC_END_OF_STACK;
            } else {
                *state->current++ = reinterpret_cast<void*>(pc);
            }
        }
        return _URC_NO_REASON;
    }

    size_t captureBacktrace(void** buffer, size_t max)
    {
        BacktraceState state = {buffer, buffer + max};
        _Unwind_Backtrace(unwindCallback, &state);

        return state.current - buffer;
    }

    void dumpBacktrace(std::ostream& os, void** addrs, size_t count)
    {
        for (size_t idx = 0; idx < count; ++idx) {
            const void* addr = addrs[idx];
            const char* symbol = "";

            Dl_info info;
            if (dladdr(addr, &info) && info.dli_sname) {
                symbol = info.dli_sname;
            }

            os << "  #" << std::setw(2) << idx << ": " << addr << "  " << symbol << "\n";
        }
    }

} // namespace

void printStackTrace(unsigned int max_frames)
{
    void* buffer[max_frames];
    std::ostringstream oss;

    dumpBacktrace(oss, buffer, captureBacktrace(buffer, max_frames));

    __android_log_print(ANDROID_LOG_DEBUG, TAG, "%s", oss.str().c_str());
}
