# PicoStateStore

`PicoStateStore` keeps one independent, power-loss-tolerant parameter journal
for each bootloader slot. Each slot owns two 4 KiB sectors. A record uses one
256-byte data page followed by a separate 256-byte commit page, so an
interrupted write is ignored at the next boot. When a sector fills, the other
sector is erased while the previous valid record remains intact.

Payloads must be deterministic structs no larger than 224 bytes. Initialize
padding and reserved fields before calling `service()` so uninitialized bytes
do not look like parameter changes.

```cpp
#include "PicoStateStore.h"

struct AppState {
  uint8_t mode;
  uint8_t reserved[3];
  uint16_t amount;
};

PicoStateStore stateStore;
AppState state = {};

void setup() {
  stateStore.begin(0x41505031u, 1, 10000);
  if (stateStore.load(&state, sizeof(state))) {
    // Apply state and engage pot pickup/locking here.
  }
}

void loop() {
  // Update state from user controls, then service from core 0.
  stateStore.service(&state, sizeof(state));
}
```

The interval is a fixed checkpoint period. Every 10 seconds the current state
is compared with the last committed state; unchanged payloads are not written.
Increment the schema version whenever the payload layout changes.
