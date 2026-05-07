# Pico-Eurorack Client

Copyright 2026 Wenhao Yang

Author: Wenhao Yang
Contributor: Wenhao Yang

Local web client for selecting bootloader app slots and generating a bundled UF2.

## Run

```sh
python3 Client/server.py
```

Open:

```text
http://127.0.0.1:8765/
```

The server provides `/api/manifest` for app size metadata and `/api/generate`
for slot-linked compilation plus bootloader packaging. Generated client outputs
are written under `Bootloader/build/client/`.
