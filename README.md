# drpc

`drpc` is a Windows tray app prototype for publishing Discord Rich Presence from mock desktop activities.

It is set up to prefer the official Discord Social SDK, but the repository builds without the SDK present so you can work on the app shell immediately. When the SDK is missing, the app runs with a no-op backend and logs what it would have published.

## What v1 does

- Runs as a background Windows tray app
- Loads `config.json`
- Cycles between editable mock presets
- Supports pause, resume, previous preset, next preset, open logs, and quit
- Publishes Rich Presence through the Discord Social SDK when the SDK is installed

## Requirements

- Windows 10 or Windows 11
- CMake 3.24+
- Visual Studio 2022 with Desktop development for C++
- Discord desktop client running for direct Rich Presence testing

## Add the Discord Social SDK

The official SDK binaries are distributed through the Discord Developer Portal after you enable the Social SDK for your application.

1. Create a Discord application in the Developer Portal.
2. Enable the Discord Social SDK for that application.
3. Download the Windows SDK package.
4. Place the package so this header exists:

```text
vendor/discord_social_sdk/include/discordpp.h
```

5. Make sure the import library is reachable from one of these locations, or pass `-DDRPC_SDK_LIBRARY=...` to CMake:

```text
vendor/discord_social_sdk/lib/
vendor/discord_social_sdk/lib/debug/
vendor/discord_social_sdk/lib/release/
```

If your SDK layout differs, configure both of these during CMake setup:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DDRPC_SDK_INCLUDE_DIR="C:/path/to/sdk/include" `
  -DDRPC_SDK_LIBRARY="C:/path/to/discord_partner_sdk.lib"
```

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## Run

```powershell
.\build\drpc.exe
```

On first run, the app creates `config.json` next to the executable if it does not exist yet.

## Configure

Update `config.json` with your real Discord application ID and asset keys. Example fields:

- `applicationId`
- `updateIntervalMs`
- `presets[].details`
- `presets[].state`
- `presets[].assets.largeImage`
- `presets[].assets.smallImage`
- `presets[].buttons`

Buttons are only visible to other users, so test with a second Discord account.

## Logs

Logs are written to:

```text
logs/drpc.log
```

Use the tray menu item to open the log file quickly.
