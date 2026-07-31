# Stream Protocol

The EDGE-VIEW runs a TCP server on port **4242** that accepts JSON commands. Multiple clients can connect simultaneously (up to 4).

## Connection

```bash
nc <pico-ip> 4242
```

On connection, the server sends a hello banner:

```json
{"type":"hello","device":"Station Pico"}
```

## Commands

### Ping
```json
{"type":"ping"}
```
Response: `{"type":"pong"}`

### Weather Update
```json
{"type":"weather","temp":25,"cond":1}
```
- `temp`: Temperature in Celsius
- `cond`: Condition code (0=Sunny, 1=Cloudy, 2=Rain, 3=Storm, 4=Snow, 5=Fog)

### Text / AI Message
```json
{"type":"text","text":"Hello from AI"}
```
Displays the message on the AI status panel.

### Sprite Control
```json
{"type":"sprite",0,100,200,2,2}
```
Move sprite at index 0 to position (100, 200) with velocity (2, 2).

### Reboot
```json
{"type":"reboot"}
```
Triggers a watchdog reboot of the Pico.

## Implementation

The stream server is implemented in `libionity/ionity_stream.c` using lwIP raw TCP API with `NO_SYS=1`.