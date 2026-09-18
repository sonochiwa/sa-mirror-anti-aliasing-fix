#pragma once

#include <cstddef>
#include <cstdint>

// Reads that fail on an unmapped page return false instead of faulting.
bool SafeCopy(uintptr_t address, void* result, size_t size);

template <typename T>
bool SafeRead(uintptr_t address, T& result) {
    return SafeCopy(address, &result, sizeof(T));
}

bool IsExecutableAddress(uintptr_t address);
bool WriteMemory(void* destination, const void* source, size_t size);

// Returns the address of the first E8 CALL in [start, start + size) whose
// target is `target`, or 0.
uintptr_t FindRelativeCall(uintptr_t start, size_t size, uintptr_t target);

// Rewrites the displacement of the E8 CALL at `callAddress` to reach
// `newTarget`. Fails when the site is not a CALL or is out of reach.
bool RedirectRelativeCall(uintptr_t callAddress, uintptr_t newTarget);
