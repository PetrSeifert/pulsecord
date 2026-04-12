# drpc

`drpc` is a Windows tray app for publishing Discord Rich Presence from desktop activity. The repo now supports two activity modes:

- `browser`: real browser activity sent from a Chromium extension through a native messaging host
- `mock`: the original preset rotator for local testing

## Repo layout

- `src/`: Windows tray app, Discord backend, browser named-pipe source, native messaging host
- `protocol/`: versioned browser activity contract shared by the app and extension
- `external/drpc-browser-extension/`: pinned git submodule for the standalone MV3 browser extension
- `scripts/Register-NativeHost.ps1`: registers the native messaging host manifest for Chromium browsers

## What browser mode does

- Keeps the tray app as the only process that talks to Discord
- Accepts normalized browser snapshots over `\\.\pipe\drpc-browser-activity`
- Uses a dedicated native messaging host executable, `drpc_native_host.exe`
- Uses bundled website definitions in the browser extension for page matching and card building
- Supports scripted activity cards for Crunchyroll, HIDIVE, Netflix, and 9anime
- Keeps the last matched site activity sticky when the user switches to an unmatched page
- Clears presence when no matched browser activity remains or cached browser data goes stale

## Requirements

- Windows 10 or Windows 11
- CMake 3.24+
- Visual Studio 2022 with Desktop development for C++
- Discord desktop client optional for OAuth overlay testing
- A Chromium-based browser for browser mode

## Clone

Clone with submodules, or initialize them after cloning:

```powershell
git clone --recurse-submodules <repo-url>
git submodule update --init --recursive
```

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Built targets:

- `build/drpc.exe`
- `build/drpc_native_host.exe`
- `build/drpc_tests.exe` when testing is enabled

## Configure

`config.json` now includes browser mode settings:

- `activityMode`: `browser` or `mock`
- `browserDetection.enabled`
- `browserDetection.staleAfterMs`
- `browserDetection.fallbackPreset`
- `browserDetection.supportedSites[]` as an optional allowlist for bundled site-definition IDs
- `discordAuth.enabled` to use account-authenticated Social SDK sessions
- `discordAuth.redirectUri` for the OAuth callback configured in the Discord developer portal
- `discordAuth.tokenStoragePath` for persisted access and refresh tokens

Default browser mode uses:

- bundled site-defined activity cards for supported browser pages
- no generic browser-card fallback when the active page is unmatched

Per-site browser detector configuration now lives in
`external/drpc-browser-extension/site-config.js`. Reload the unpacked extension
after editing it.

## Run browser mode

1. Build the tray app and native host.
2. Load `external/drpc-browser-extension/` as an unpacked extension.
3. Copy the extension ID from the browser's extensions page.
4. Register the native host:

```powershell
.\scripts\Register-NativeHost.ps1 `
  -HostPath .\build\drpc_native_host.exe `
  -ExtensionIds <extension-id> `
  -Browsers chrome,edge
```

5. Start the tray app:

```powershell
.\build\drpc.exe
```

6. Start playback in the active browser tab.

## Discord authentication

The tray app now defaults to the Discord Social SDK account authentication flow. For public-client
setups, enable `Public Client` in your application's OAuth2 settings and add
`http://127.0.0.1/callback` as a redirect URL in the Discord developer portal. The app stores
access and refresh tokens in the path configured by `discordAuth.tokenStoragePath`, protects them
with Windows DPAPI, refreshes them when needed, and falls back to a browser-based sign-in if the
Discord desktop client is not running.

## Discord Social SDK

The app still prefers the official Discord Social SDK, but the repo builds without it so the tray app and native messaging flow can be developed immediately. When the SDK is missing, the app logs what it would have published.

Add the SDK under `vendor/discord_social_sdk/` or configure:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DDRPC_SDK_INCLUDE_DIR="C:/path/to/sdk/include" `
  -DDRPC_SDK_LIBRARY="C:/path/to/discord_partner_sdk.lib"
```

## Tests

Native tests:

```powershell
ctest --test-dir build --output-on-failure
```

Extension tests:

```powershell
cd external/drpc-browser-extension
npm test
```

## Logs

Tray logs are written to:

```text
logs/drpc.log
```
