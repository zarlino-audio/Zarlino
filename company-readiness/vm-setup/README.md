# Zarlino VM setup kit

One file to turn a fresh Windows VM into the Zarlino **always-on host**:
`ZarlinoVM-Setup-<version>.exe`, downloadable from the OS console (Files -> "Machine setup (VM)").

**How the exe behaves:** double-click -> UAC prompt -> it unpacks the setup script to
`%LOCALAPPDATA%\Zarlino\setup\` and runs it in a visible console, asking for the founder token.

**Why a C# launcher and not NSIS:** the first build was an NSIS installer and Windows Defender
blocked it as `Trojan:Win32/Bearfoos.A!ml` - the well-known machine-learning false positive on
unsigned installer stubs. A managed PE built with the in-box compiler is not flagged, so the kit
ships that instead. Signed NSIS can return once a code-signing certificate exists (audit LGL-05).

## What the installer does

Run it inside the VM, paste the founder token, and it:
1. checks admin rights, disk space and network to `os.zarlinoaudio.com`;
2. installs **Node.js LTS** if the machine has none (winget first, official MSI fallback);
3. downloads the executor + WhatsApp-bridge payload **from the OS** (founder-authenticated);
4. lays it out under `C:\Zarlino\exec-os` and runs `npm ci` for the executor and the bridge;
5. writes local config (`exec-os\.dev.vars`, bridge `.env`) with ACLs limited to
   Administrators + SYSTEM - **no secret is stored in the exe**;
6. registers hidden logon auto-start launchers for both processes
   (`C:\Zarlino\launchers\*.vbs` -> Startup folder);
7. starts them and verifies: QR page answers on `http://localhost:3037/`, daemon log written,
   and the OS monitor reports the bridge heartbeat;
8. prints the one remaining manual step: **pair WhatsApp** (scan the QR once).

It is idempotent - re-run it any time - and supports `-DryRun` to preview every action
without changing anything.

## What it deliberately does not do

| Not included | Why |
|---|---|
| Creating/starting the cloud VM | that stays an explicit (paid) cloud action, not something an installer does behind your back |
| Any embedded secret | the token is typed by you and stored only on the machine, ACL-restricted |
| Visual Studio / Ninja toolchain | builds and renders still belong to the laptop until the cloud build story exists |
| Mac/AU building | unrelated to a Windows VM |

A VM host gives you: the always-on **WhatsApp founder channel**, an executor that heartbeats
into the OS, and dispatch/soak work. It does not give you plugin builds.

## Build and publish

```powershell
cd C:\Users\khrit\Desktop\plugin\company-readiness\vm-setup
powershell -ExecutionPolicy Bypass -File .\build-vm-setup.ps1 -Version 1.0.0 -Publish
```

That produces locally:

| File | Purpose |
|---|---|
| `ZarlinoVM-Setup-<ver>.exe` | the installer (NSIS) |
| `zarlino-vm-payload.zip` | exec-os source the installer pulls (no secrets, no `node_modules`) |
| `vm-setup-manifest.json` | version, sizes, sha256 of both files |

and, with `-Publish`, uploads all three to the OS as `artifacts/vm-setup/*`, where the console
serves them with the founder bearer token.

## Verify a download

```powershell
$token = (Get-Content C:\Users\khrit\Desktop\zarlino-executive-os\.dev.vars | Where-Object { $_ -like 'FOUNDER_AUTH_TOKEN=*' }) -replace '^FOUNDER_AUTH_TOKEN=',''
$list  = Invoke-RestMethod 'https://os.zarlinoaudio.com/api/artifacts?prefix=artifacts/vm-setup/' -Headers @{ Authorization = "Bearer $token" }
$exe   = ($list.artifacts | Where-Object { $_.key -like '*.exe' } | Sort-Object uploaded -Descending | Select-Object -First 1).key
Invoke-WebRequest ("https://os.zarlinoaudio.com/api/artifacts/" + $exe) -Headers @{ Authorization = "Bearer $token" } -OutFile .\vm-setup.exe
(Get-FileHash .\vm-setup.exe -Algorithm SHA256).Hash   # must equal vm-setup-manifest.json
.\vm-setup.exe --version            # prints the baked-in version, does nothing else
.\vm-setup.exe --extract-only       # unpacks the script without running it
```

The bootstrap resolves the newest `*zarlino-vm-payload.zip` under `artifacts/vm-setup/` at run
time, so the exe never needs rebuilding when only the payload changes.

## Recovery / re-run

- Everything is logged to `%TEMP%\zarlino-vm-setup.log`; process logs live in `C:\Zarlino\*.log`.
- To preview: `powershell -ExecutionPolicy Bypass -File C:\Zarlino\setup\bootstrap.ps1 -DryRun`
- To skip dependency installs (already installed): add `-SkipDeps`.
- To install without starting: add `-NoStart`.
- To remove the startup entries: delete the two shortcuts named `Zarlino *(VM)*` from the
  Startup folder (nothing else is installed outside `C:\Zarlino`).
