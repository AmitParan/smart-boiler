# Smart Boiler

This repository contains two ESP32 firmware projects:

- `master/` - the master/display controller firmware
- `slave/` - the slave/controller firmware

## Build

Build each project from its own folder:

```powershell
cd master
platformio run

cd ..\slave
platformio run
```
