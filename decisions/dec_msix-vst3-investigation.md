# Decision: Can Zarlino plugins be packaged with MSIX and behave like the .exe?

**Date:** 2026-08-29
**Status:** Investigation complete — **MSIX is NOT suitable for VST3 delivery.** MSIX is only viable for the Standalone .exe, and even then it does not behave identically to a plain exe.

---

## TL;DR

| Deliverable | MSIX possible? | Behaves like exe? |
|---|---|---|
| VST3 plugin (`.vst3` bundle) | ❌ No — DAWs cannot load it | N/A |
| Standalone `.exe` (JUCE Standalone) | ✅ Yes | ⚠️ Mostly, with caveats (see below) |

**Verdict:** Keep the traditional installer (NSIS/Inno/MSI) for VST3. Optionally add MSIX **only for the Standalone** if Store / App Installer distribution is wanted. Do not attempt MSIX for the VST3 payload.

---

## Part 1 — Why VST3 cannot be shipped via MSIX

### 1. MSIX installs to a protected, read-only, versioned path
- Packages install per-user to `C:\Program Files\WindowsApps\<PackageFullName>\` (e.g. `..._1.0.0.0_x64__8wekyb3d8bbwe`), files are marked read-only and ACL-locked.
- The package full name includes the **version and a hash**, so the path changes on every update.

### 2. MSIX file-system virtualization is per-process — DAWs can't see it
- The `VFS\` overlays (`VFS\ProgramFilesCommonX64`, etc.) are merged **only for the packaged app's own process**. The DAW is an **unpackaged** process: it sees neither the VFS overlay nor the package's files (ACL denies access).

### 3. VST3 discovery is file-path based (no registration)
Per Steinberg's VST3 dev portal, hosts recursively scan only these folders on Windows:
1. **User:** `%LOCALAPPDATA%\Programs\Common\VST3\`
2. **Global:** `C:\Program Files\Common Files\VST3\` (and x86 variant)
3. **Application:** `$APPFOLDER\VST3\`

MSIX cannot place files in these fixed/shared folders, and has **no install-time custom action** to copy them there.

### 4. Even a manual "custom VST path" pointed at WindowsApps fails
- The WindowsApps folder is ACL-protected; unpackaged processes get access denied.
- The versioned path breaks after every MSIX update (the saved DAW path would point at a deleted folder).

### 5. Confirmed by Microsoft
Microsoft Q&A thread *"Specific questions about Windows 11 Store Deployment — MSIX, signing, EXE and VST3 (DLL)"* (learn.microsoft.com, question 543703) — the exact scenario (deploy a signed VST3 DLL to an industry-standard folder via MSIX, and load third-party VST3 DLLs from it) is **not supported**. The official suggestion was to publish the win32 app unpackaged.

### 6. Industry practice agrees
NI (Native Access), iZotope, etc. ship VST3 via their own installers, not MSIX — because MSIX targets *apps*, not shared/system components.

---

## Part 2 — The Standalone .exe via MSIX: what changes

Technically possible (JUCE Standalone is a normal Win32 exe). But it is **not** byte-for-byte "the same as an exe":

| Aspect | Plain exe | MSIX-packaged exe |
|---|---|---|
| Install location | Anywhere | `WindowsApps\...` (read-only) |
| Write next to exe | OK | ❌ Not allowed (package is read-only) |
| Settings in `%APPDATA%` | Real path, shared | Virtualized to per-package state — **invisible to other apps**, cleaned on uninstall |
| Registry (HKCU) | Real | Virtualized per-user-per-package |
| Signing | Optional (SmartScreen risk) | **Required** (self-signed cert must be trusted on the machine to sideload) |
| Updates | Manual / installer | Auto via Store or App Installer (differential) |
| Uninstall | Installer-dependent | Clean, but only removes package contents |
| Audio device access | Normal | Needs `runFullTrust` (mediumIL) — `appContainer` is bad for audio (latency/device access) |

**Critical behavioral gotcha:** the JUCE Standalone writes its settings to `%APPDATA%\<Company>\<App>`. Under MSIX these writes are redirected to the package's private state, so a machine with **both** the MSIX Standalone and the normally-installed VST3 of the same plugin would have **two divergent settings stores** that don't share state.

---

## Part 3 — Workarounds (if MSIX/Store distribution is ever required)

1. **MSIX only for the Standalone; NSIS/Inno/MSI stays for VST3** — the standard industry pattern. Recommended.
2. **MSIX + runFullTrust bootstrapper** that copies the bundled `.vst3` into `C:\Program Files\Common Files\VST3` on first launch. Dirty: needs elevation, breaks "clean uninstall" (copied file isn't tracked), and plugin appears only after first run.
3. **Package Support Framework (PSF)** — redirects writes *inside* the package to a writable location; makes the Standalone closer to exe behavior, but does nothing for DAW visibility of the VST3.

---

## Recommendation for this repo

- **Keep the existing installer-based VST3 distribution** (zips + NSIS) — it is the only mechanism DAWs reliably see.
- Do **not** pursue MSIX for the `.vst3` bundle. If Store distribution is ever wanted, ship the Standalone as MSIX with `runFullTrust` and keep VST3 on the web/installer.
- If an MSIX Standalone is built: verify JUCE settings path persistence (AppData redirection), and test audio device access with full trust.
