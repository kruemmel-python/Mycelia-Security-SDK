# Mycelia Chat Server (TCP)

Dieser Server vermittelt ausschließlich verschlüsselte Payloads. Er kennt keinen Klartext.

## Start

```bash
python3 tools/server/mycelia_chat_server.py
```

Standard-Port: `8989` (bindet an `0.0.0.0`).

## Protokoll

- TCP
- Frame: 4-Byte Länge (u32 big-endian) + JSON Payload
- JSON:
  - `type`: `join` oder `message`
  - `roomId`: Invite Code (Base64 Seed)
  - `bodyCipherBase64`: Ciphertext (nur bei `message`)
  - `counter`: Message Counter (nur bei `message`)

Der Server leitet Frames innerhalb desselben `roomId` an alle Clients weiter.
