// heap_probe.h - ESP8266 memory diagnostic for burning_dome
//
// Probe the metric that PREDICTS alloc failure: the LARGEST CONTIGUOUS
// block, not the sum of free bytes. The ESP8266 heap is non-compacting,
// so getFreeHeap() can report plenty while getMaxFreeBlockSize() has
// fragmented below what a String/HTML build needs. (Same lesson as the
// KEXP CircuitPython GC.)
//
// Usage: #include "probes/heap_probe.h" in burning_dome.ino, call
// heapProbe("boot") in setup() and heapProbe("soak") periodically.
// Control (see probes/README.md): hammer "/" in a loop and confirm the
// max-block number actually moves - if it never moves, the probe isn't
// observing the fragmentation path.

#pragma once
#include <Arduino.h>

inline void heapProbe(const char* tag) {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint16_t maxBlock = ESP.getMaxFreeBlockSize();  // predicts failure
  uint8_t  frag     = ESP.getHeapFragmentation(); // 0 = none, 100 = worst
  Serial.printf("[heap:%s] free=%u  maxblock=%u  frag=%u%%\n",
                tag, freeHeap, maxBlock, frag);
}
