#include "patch.h"

#include <windows.h>

#include <cstring>
#include <limits>

bool SafeCopy(uintptr_t address, void* result, size_t size) {
    __try {
        memcpy(result, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsExecutableAddress(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info = {};
    if (!address ||
        VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info)) {
        return false;
    }

    constexpr DWORD executablePages =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & executablePages) != 0;
}

bool WriteMemory(void* destination, const void* source, size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);

    DWORD unused = 0;
    VirtualProtect(destination, size, oldProtect, &unused);
    return true;
}

uintptr_t FindRelativeCall(uintptr_t start, size_t size, uintptr_t target) {
    for (size_t offset = 0; offset + 5 <= size; ++offset) {
        uint8_t opcode = 0;
        if (!SafeRead(start + offset, opcode) || opcode != 0xE8) {
            continue;
        }

        int32_t displacement = 0;
        if (!SafeRead(start + offset + 1, displacement)) {
            continue;
        }
        if (start + offset + 5 + displacement == target) {
            return start + offset;
        }
    }
    return 0;
}

bool RedirectRelativeCall(uintptr_t callAddress, uintptr_t newTarget) {
    uint8_t call[5] = {};
    if (!SafeCopy(callAddress, call, sizeof(call)) || call[0] != 0xE8) {
        return false;
    }

    const intptr_t distance = newTarget - (callAddress + sizeof(call));
    if (distance < std::numeric_limits<int32_t>::min() ||
        distance > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    const int32_t displacement = static_cast<int32_t>(distance);
    memcpy(&call[1], &displacement, sizeof(displacement));
    return WriteMemory(reinterpret_cast<void*>(callAddress), call, sizeof(call));
}
