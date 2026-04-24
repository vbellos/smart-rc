// Minimal Arduino stub so Steering can be unit-tested on the host.
#pragma once

#include <stdint.h>

namespace smartrc_native_test {
extern uint32_t g_now_ms;
}

inline uint32_t millis() { return smartrc_native_test::g_now_ms; }
inline void     delay(uint32_t)  {}

#define HIGH 1
#define LOW  0
#define OUTPUT 1

inline void pinMode(uint8_t, uint8_t)        {}
inline void digitalWrite(uint8_t, uint8_t)   {}
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline uint32_t ledcWrite(uint8_t, uint32_t) { return 0; }
