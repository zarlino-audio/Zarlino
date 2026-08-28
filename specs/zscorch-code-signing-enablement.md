# ZScorch Code-Signing Enablement

**Status:** CONFIGURED · **Gateable at:** Gate 7 (Security & Licensing) & Gate 9 (Packaging)
**Risk it resolves:** `risk_zscorch_signing` (MEDIUM — ZScorch release build unsigned)
**Tracking task:** `task_c7fb77cf`
**CI config:** `.github/workflows/zscorch-release-gate.yml` (branch `feat/zscorch-ci-code-signing`)

---

## 1. What is configured (FACT)

A ZScorch release-gate GitHub Actions workflow now exists with an **inert, gated** Authenticode (Windows) code-signing pipeline. All signing steps are wrapped in `if: ${{ vars.ZARLINO_SIGNING_ENABLED == 'true' }}`, so they do nothing until deliberately enabled — this keeps the release build working today with zero disruption while the certificate spend is deferred (D-007 cash protection / D-012 prioritize cash generation).

The workflow:
1. Configures the CMake build (Windows x64, `BUILD_TESTING=OFF` — ZScorch's `ZScorch_Tests` target is currently disabled in its CMakeLists).
2. Builds the ZScorch VST3 + Standalone targets in Release.
3. **Signs** the VST3 module DLL and the standalone exe in place via `Azure/artifact-signing-action@v3` (SHA-256 digest, RFC3161 timestamp).
4. **Verifies** the Authenticode signatures with `Get-AuthenticodeSignature`.
5. Packages and uploads the signed VST3 + Standalone artifacts.

This mirrors the ZTame signing pipeline already scaffolded on `chore/ci-code-signing-pipeline` for consistency.

---

## 2. What "enabled" requires (Founder + spend + secrets — NOT DONE, deliberately)

Signing is inert until BOTH are true:

### a) Spend decision — Founder approval required (NOT approved, blocked by D-007/D-012)
Provision an Azure Trusted Signing account + certificate profile (Microsoft's managed code-signing, ~US$9.99/month basic plan; no annual certificate purchase). At current cash of **$1.60**, this spend is deferred **until revenue justifies it**. An Authenticode trust anchor is what removes the SmartScreen "Unknown publisher" warning and the need for the unsigned/disclosed-trial disclosure on the download pages.

- **Delegated spend authority:** $25/mo autonomous max; spend approval threshold $100. Provisioning Azure Trusted Signing (~$10/mo) is **below** the $100 threshold but IS a recurring subscription — per D-003/D-007 it must be tied to a measurable hypothesis (e.g. "signed installer → materially higher conversion from the landing page") and Founder approval.

### b) Repository secrets + variable — Founder supplies (NOT set)
The workflow reads 7+1 GitHub configuration values that must exist before enabling:

| Config | Type | Value source |
|---|---|---|
| `ZARLINO_SIGNING_ENABLED` | repo/org **variable** | set to `true` to arm signing |
| `AZURE_TENANT_ID` | secret | Azure AD tenant |
| `AZURE_CLIENT_ID` | secret | Azure AD app registration (service principal) |
| `AZURE_CLIENT_SECRET` | secret | same app principal |
| `AZURE_SIGNING_ENDPOINT` | secret | `https://eus.codesigning.azure.net` (region per account) |
| `AZURE_SIGNING_ACCOUNT` | secret | Trusted Signing account name |
| `AZURE_CERTIFICATE_PROFILE` | secret | certificate profile name |

These secrets require real Azure credentials held only by the Founder; the Executive cannot and must not fabricate them.

---

## 3. Enablement step-by-step (unblock path)

1. **Founder approves the spend** (recurring Azure Trusted Signing ~$10/mo) once revenue/traffic justifies the hypothesis (D-003/D-007).
2. **Founder** provisions the Trusted Signing account, registers the service principal, and:
   - sets the 6 `AZURE_*` **secrets**, and
   - sets the **variable** `ZARLINO_SIGNING_ENABLED=true` (repo or org level) in GitHub.
3. Merge this PR (release-gate workflow) **before** enabling, so enabling is a one-variable flip.
4. On the next ZScorch release run, the workflow signs + verifies. Confirm the artifacts' signature status = `Valid` and SmartScreen behavior clears.
5. **Close** `risk_zscorch_signing` and unblock ZScorch **Gate 7** (Security & Licensing) and **Gate 10** (Human Release Gate).
6. Retire the unsigned-disclosed-trial disclosure on the ZScorch download page once signed builds ship.

---

## 4. Gates & doctrine compliance

- **D-006 (minimal sufficient complexity):** reuses the exact gated pattern already scaffolded for ZTame; no bespoke signing machinery.
- **D-007 / D-012 (protect cash / prioritize cash):** the pipeline is inert and costs nothing until deliberately enabled; no spend occurs now.
- **D-008 (reversible experiments):** enabling is a one-variable flip, fully reversible by flipping `ZARLINO_SIGNING_ENABLED=false`.
- **D-002 (measure before optimize):** do not provision the certificate until a falsifiable conversion hypothesis exists.

---

## 5. Not in scope / explicitly NOT done

- Purchasing/provisioning the certificate/account (spend, Founder).
- Setting the `AZURE_*` secrets / the `ZARLINO_SIGNING_ENABLED` variable (Key M I, Founder-held).
- Signing mac/Apple builds (Apple Developer Program cert, separate $99/yr) — out of scope for this Windows Authenticode gate.
- Fabricating fake secrets or claiming signing is live — signing remains **inert/configured**, not active.
