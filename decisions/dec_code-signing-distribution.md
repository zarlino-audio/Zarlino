# Decision: Avoiding the ~$150/yr code-signing cost while keeping distribution smooth & trusted

**Date:** 2026-08-29
**Status:** Investigation complete.

---

## Executive summary

| Option | Cost | What it buys | Fits this repo? |
|---|---|---|---|
| **Microsoft Store dev account + MSIX** | **Free** (no registration fee, 2025+ flow) | Store-signs your package, trusted by default, auto-update, clean uninstall | ✅ **Standalone only** — VST3 cannot ship via MSIX (DAWs can't load it) |
| **SignPath Foundation** | **Free** (open-source projects only) | Authenticode signing on their HSM; public-trust root → no "Unknown Publisher"; CI-integrated | ⚠️ Only if a product is open-sourced (repo currently has no LICENSE) |
| **Budget CA OV/IV cert** (SSL.com $129/yr; Certum typically cheaper) | ~$96–130/yr + mandatory HSM/token or cloud-signing | Publisher name displays (no "Unknown Publisher"); SmartScreen reputation builds over time | ✅ Closed-source exe/installer |
| **Azure Artifact Signing** (Microsoft) | $9.99/mo ≈ $120/yr | Fully managed HSM signing, cert lifecycle handled, CI-integrated, Microsoft root | ✅ Closed-source; zero-ops |
| **Self-signed cert** | Free | Nothing for end users — still "Unknown Publisher", untrusted | ❌ Internal/testing only |
| **No signing** | Free | SmartScreen "Unknown Publisher" + hard warning | ❌ Not "smooth" |

---

## The reality check: what the $150 cert actually buys

- **Since March 2024, EV and OV are treated equally by SmartScreen.** Microsoft no longer grants instant reputation to EV. **Reputation is built from clean-download volume**, regardless of cert type (confirmed by SSL.com's code-signing FAQ; consistent with Microsoft docs).
- Any cert chained to a trusted root **removes "Unknown Publisher"** (your name displays) and softens the SmartScreen warning to "unrecognized app" with a "More info → Run anyway" path. That is the main benefit of paying.
- A brand-new OV/IV cert starts with **no reputation** — users may still see a SmartScreen warning until trust accumulates (a renewed cert typically rebuilds reputation in days / a few hundred clean downloads).
- **June 2023 CA/B Forum mandate:** code-signing private keys must be hardware-backed (FIPS token or cloud HSM). That means a traditional cert effectively requires a token (~$300+ one-time) or a cloud-signing subscription — narrowing the price gap vs managed services.

**Bottom line: paying $150 does not give instant "trusted" status.** It removes "Unknown Publisher" and enables reputation to accumulate. If the goal is "zero warnings from day one," the only channel that does that is the Microsoft Store.

---

## The free routes (both verified)

### 1. Microsoft Store developer account — now FREE
- The new onboarding flow (via `storedeveloper.microsoft.com`) has **no registration fee for either Individual or Company** accounts (legacy $19/$99 fee removed).
- Store signs your MSIX packages with Microsoft's certificate → users get a **trusted, auto-updating, cleanly-uninstallable** app with no SmartScreen friction.
- **Constraint (ties into the prior MSIX investigation):** DAWs cannot load VST3 from an MSIX package, so this route covers only the JUCE **Standalone** `.exe`. The VST3 payload still needs the NSIS/zip route.

### 2. SignPath Foundation — free Authenticode for open-source projects
- `signpath.org` provides free code signing to OSS projects: private key held on their HSM, integrated into GitHub Actions/Jenkins/Azure DevOps, and the certificate vouches that the binary was built from your public repository.
- Requirement: a **public, genuinely open-source repository**. This repo currently has **no LICENSE** → at least one product would need to be open-sourced to qualify.
- If OSS is acceptable, this is the single best free answer: real Authenticode on your NSIS installer + standalone exe, no "Unknown Publisher".

---

## Cloud code-signing price comparison (key on cloud HSM, no USB token) — verified 2026-08-29

| # | Service | Price | Notes |
|---|---|---|---|
| 1 | **SignPath Foundation** | **$0** | Free for open-source projects. HSM-held key, GitHub Actions/Jenkins/Azure integration, public-trust chain. Requires genuinely OSS repo (none currently). |
| 2 | **Azure Artifact Signing** | **~$120/yr** ($9.99/mo) | Cheapest paid option. Managed cert + FIPS HSM + lifecycle + CI, Microsoft trust root, 5,000 sigs/mo included ($0.005/sig after). **Under the $150 budget, zero add-ons.** |
| 3 | **Certum** | **€25/yr Open Source / €139/yr Standard** | Cheapest certs. Open Source tier (OSS only) €25; Standard (individual or company) €139 ≈ $150 (likely the quote in hand), with **cloud-based availability** via CertManager (no token). EV €329. |
| 4 | SSL.com eSigner for Code | **~$309/yr** | Tier 1 cloud signing $180/yr ($15/mo, 240 sigs) **+** IV/OV cert $129/yr. Pricier than the top 3; 30 days free. |

## Paid but cheaper / simpler than $150

- **Budget CAs:** SSL.com OV and IV are both **$129/yr** (multi-year ~$96–116/yr); Certum Standard **€139/yr** (cloud-based). All now require a FIPS token or cloud-signing subscription per the 2023 mandate.
- **Azure Artifact Signing:** **$9.99/mo** base ($120/yr, 5,000 signatures/mo, $0.005/sig after). Fully managed: HSM, certificate lifecycle, CI integration, Microsoft public-trust chain. Slightly cheaper than the $150 OV **and** includes what the OV would need anyway (HSM/cloud signing) with zero ops.

---

## Free "trust multipliers" (no cert required)

- **Consistent publisher identity** in every binary (CompanyName / ProductName / FileVersion) — reputation and allowlisting follow the publisher, so consistency matters as much as the cert.
- **Timestamp every signature** so signatures outlive cert expiry.
- Publish **SHA-256 hashes** and a verification note on the website.
- Distribute via **winget / Scoop / Chocolatey** for power users and IT (trust comes from the package manager; also GPO-allowlistable).
- Drive downloads from **stable URLs** to build SmartScreen reputation.
- Note: the **`.vst3` file itself is not SmartScreen-scanned** — DAWs load it without signature checks. Only the installer `.exe` and the standalone `.exe` need signing.

---

## Recommendation for Zarlino

1. **If any product can be open-sourced** → apply to **SignPath Foundation**: free, reliable, removes "Unknown Publisher". Best free path for the NSIS installer + standalone.
2. **Standalone distribution** → **free Store account + MSIX**: trusted by default, auto-updating, zero cost.
3. **Closed-source exe/installer** → pick the cheapest OV/IV with cloud signing, or **Azure Artifact Signing**; accept that reputation (download volume), not the cert, removes the last SmartScreen warning; keep the publisher identity consistent across releases.
4. **Avoid:** EV certs (no longer grant instant reputation — wasted money since 2024) and self-signed certs (untrusted; internal use only).

---

## Sources
- SSL.com eSigner for Code pricing: Tier 1 $180/yr ($15/mo, 240 signings) + certificate $129/yr. https://www.ssl.com/products/software-integrity/signing-service/
- Certum code-signing family: Open Source from €25, Standard from €139, EV from €329; cloud-based availability via CertManager. https://www.certum.eu/en/code-signing-certificates/
- Azure Artifact Signing: $9.99/mo base SKU. https://azure.microsoft.com/en-us/pricing/details/artifact-signing/
- Microsoft Learn: "Steps to open a Microsoft Store developer account" — no registration fees for Individual or Company (new flow). https://learn.microsoft.com/en-us/windows/apps/publish/partner-center/open-a-developer-account
- SignPath Foundation — free code signing for OSS. https://signpath.org/
- Azure Artifact Signing pricing — $9.99/mo base SKU. https://azure.microsoft.com/en-us/pricing/details/artifact-signing/ and https://learn.microsoft.com/en-us/azure/artifact-signing/overview
- SSL.com code-signing FAQ — EV and OV treated equally since March 2024; reputation builds via download volume; renewed certs rebuild reputation faster. https://www.ssl.com/certificates/code-signing/
- Prior investigation: `decisions/dec_msix-vst3-investigation.md` (why VST3 can't ship via MSIX).
