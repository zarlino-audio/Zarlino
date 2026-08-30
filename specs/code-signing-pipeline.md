# Code-Signing Pipeline (Authenticode) - Zarlino Windows Catalog
**Status:** CONFIGURED-READY / INERT (gate disabled). Certificate purchase DEFERRED per Founder approval apr_51e79926. Cash $1.60; D-007 protect cash; D-012 cash>reputation.

## 1. Goal
Authenticode-sign the Windows binaries we ship so Windows SmartScreen/OS trust warnings do not erode trust or burden support. One certificate covers the whole plugin line: ZTame (SHIPPED), ZScorch (SHIPPED), PresenceField (in pipeline), and future catalog.

## 2. What gets signed (order matters)
For each plugin build (paths shown for the ZTame example in the release workflow):
1. The VST3 module DLL(s) under build/plugins/ZTame/ZTame_artefacts/Release/VST3/ and the standalone ZTame.exe - signed IN-PLACE before NSIS packaging so the installer embeds signed binaries.
2. The NSIS installer artifacts/ZTame-v1.0.0-Windows-Installer.exe - signed AFTER NSIS compiles (the stub must be signed, not only the inner payload).
Digest: SHA256 (file-digest) with RFC3161 timestamp (the action defaults to a Microsoft timestamp service).

## 3. Recommended: Azure Artifact Signing (formerly Trusted Signing)
- Microsoft fully-managed signing: HSM-backed short-lived certs, no cert to buy/store/renew.
- ~$10/mo (Basic: 5,000 signatures/month, 1 certificate profile). GA since ~Jan 2026.
- Integrates with GitHub Actions via the official Azure/artifact-signing-action (formerly azure/trusted-signing-action).
- Billing-account legal name must match the publisher name shown on the cert.
- Roles: Artifact Signing Identity Verifier (validation, portal only) and Artifact Signing Certificate Profile Signer (to sign).
Alternative (fallback): traditional OV/EV code-signing cert ~$108-400/yr via Certum / SSL.com / eSigner cloud.

## 4. GitHub configuration to ENABLE signing
When the Founder decides to spend (founder decision; D-003):
### Secrets
- AZURE_TENANT_ID - Azure AD tenant id.
- AZURE_CLIENT_ID - client application id.
- AZURE_CLIENT_SECRET - client secret.
- AZURE_SIGNING_ENDPOINT - signing account endpoint URI.
- AZURE_SIGNING_ACCOUNT - the signing account name.
- AZURE_CERTIFICATE_PROFILE - the certificate profile name.
### Variable
- ZARLINO_SIGNING_ENABLED = true   <- THE MASTER SWITCH; steps inert until this.
### RBAC pre-req
az role assignment create --assignee <objectId> --role "Artifact Signing Certificate Profile Signer" --scope "/subscriptions/<sub>/resourceGroups/<rg>/providers/Microsoft.CodeSigning/codeSigningAccounts/<account>/certificateProfiles/<profile>"

## 5. Enablement runbook
1. Founder approves purchase (spend gate; currently deferred).
2. Provision account + Public Trust profile + org identity validation under exact legal publisher name.
3. Set the 6 secrets + ZARLINO_SIGNING_ENABLED=true.
4. Re-run ztame-release-gate.yml (workflow_dispatch or tag push).
5. Verify: signtool verify /pa /v artifacts/ZTame-v1.0.0-Windows-Installer.exe and Get-AuthenticodeSignature on VST3 DLL + exe.
6. Relabel ZTame from disclosed-trial to non-beta GA (plan with risk_ztame_signing).

## 6. Risks/tradeoffs
- Signing does not instantly build SmartScreen reputation; it ramps with downloads.
- Deferred signing keeps trust-warning friction; keep disclosed-trial label until it lands.
- One cert/profile covers the whole catalog - buy once, use for all plugins.
