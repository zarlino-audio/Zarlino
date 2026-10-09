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

## Build host (`-BuildHost`) - moving the toolchain off the laptop

Answer **y** to the build-host question (or pass `-BuildHost`, or run `provision-buildhost.ps1`
directly) and the same machine also becomes the plugin build host. Stages are idempotent and
individually runnable with `-Stage <name>` (also `-Stage all`):

| Stage | What it does | Notes |
|---|---|---|
| `toolchain` | VS 2022 **Build Tools** with the C++ workload, CMake, Ninja, NSIS, Git | winget first, direct-download fallback; needs **elevation** - the exe already requests it |
| `repo` | clone `zarlino-audio/Zarlino` **including the JUCE submodule** | needs a GitHub token (prompted once, stored in the machine environment) |
| `env` | writes `C:\Zarlino\build-env.ps1` (VsDevCmd + PATH) and `C:\Zarlino\build-plugin.ps1` | lets the OS daemon build without an interactive shell |
| `verify` | cmake configure -> `ninja <Plugin>_All` -> `ctest -L release-gate` | **the acceptance evidence** - a real build must pass here |
| `backup` | nightly 02:30 code bundle + config copy, 14-day retention | the job that used to run on the laptop |

Log: `C:\Zarlino\buildhost.log`. Preview: `-DryRun`. Run stages later, e.g.
`powershell -File C:\Zarlino\provision-buildhost.ps1 -Stage toolchain`.

## What it deliberately does not do

| Not included | Why |
|---|---|
| Creating/starting the cloud VM | that stays an explicit (paid) cloud action, not something an installer does behind your back |
| Any embedded secret | the tokens are typed by you and stored only on the machine, ACL-restricted |
| Mac/AU building | unrelated to a Windows VM |
| Silent elevation | the toolchain stage needs admin; the exe asks for it up front instead of trying to bypass it |

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
