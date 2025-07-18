// Mock Zephyr printk.h for compilation testing
#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock printk as printf for testing
#define printk printf

#ifdef __cplusplus
}
#endif