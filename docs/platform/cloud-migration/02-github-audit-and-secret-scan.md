# 02 — GitHub Audit & Secret Scan (Phase 2 input)

- **Date**: 2026-09-18
- **Scope**: `zarlino-audio/Zarlino` (root monorepo), `zarlino-audio/zarlino-website`, local working trees, commit history (`git log -S` on key strings), ignore rules, live GitHub API (read-only).
- **Method**: pattern scan (private-key blocks, provider token formats, auth headers, credential assignments) over 576 text files (excluding `node_modules`, build outputs, vendored JUCE); git tracking + history checks; GitHub API repo/branches/issues/PRs/releases/workflows. Secret values are **never reproduced in this report** (redacted to 2–10 leading chars + length).
- **JUCE** (`juce-framework/JUCE`) was excluded from secret scanning — third-party upstream clone, not Zarlino-owned.

## Repo-level audit (VERIFIED via GitHub API)

| Item | Result |
| --- | --- |
| Visibility | **private** (already) — mandate §6 "transition public repo to private" is ALREADY DONE |
| Default branch | `main`; local `main` == `origin/main` (`0d56116c`) |
| Last push | 2026-08-30T12:14:56Z (3 weeks of local WIP not yet pushed) |
| Branches | 14 (incl. `chore/ci-code-signing-pipeline`, `wip/zscorch-dsp-reengineer`, `qa/*`) |
| Issues / PRs / Releases | 12 open issues / 8 open PRs / **0 releases, 0 tags** |
| Workflows | `executive-os-ci` (active), `ZTame Release Gate` (active), `ZScorch Release Gate` (active; not on `main` tree) |
| Local tree drift | 262 uncommitted entries (26 M / 231 ??); `.github/workflows/executive-os-ci.yml` deleted locally (uncommitted) |
| Website repo | `zarlino-audio/zarlino-website`, `main`, HEAD `4c56011` (2026-09-11), 11 uncommitted local changes |
| Actions secrets (names) | NOT VERIFIED — current fine-grained token lacks admin scope (empty response ≠ absence) |

## Secret-scan findings

| ID | Severity | Finding | Evidence | Status |
| --- | --- | --- | --- | --- |
| SEC-1 | **HIGH** | `ADMIN_TOKEN` literal committed to **`zarlino-website/wrangler.toml`** (Worker *var*, so also visible in the CF dashboard) and present in repo history (affiliate commits `a9fb6a5`, `6d0b418`, `422bc62`, `fc2ad7e`, `109f28a`, `92a73b2`) | tracked file + `git log -S"ADMIN_TOKEN"` | **OPEN — rotate + move to worker secret** |
| SEC-2 | MEDIUM | Live credentials sit in `executive-os/.dev.vars`: DeepSeek key (`sk-…`, 35), GitHub classic PAT (`ghp_…`, 40), Paystack secret (`sk_live_…`, 48), founder token, plus an **invalid** `CF_API_TOKEN` | local file; file is git-ignored (`.gitignore:2`) and was never committed | Contained locally; **rotate after migration + decommission** |
| SEC-3 | MEDIUM | Hard-coded internal shared secret `INTERNAL_SECRET` in `executive-os/src/index.ts` (also in built `dist-worker/index.js`, untracked) — gates internal scheduled-trigger calls | source scan | **OPEN — move to `wrangler secret` + rotate** |
| SEC-4 | LOW (info) | `PAYSTACK_PUBLIC_KEY` (`pk_live_…`) committed in `zarlino-website/wrangler.toml` | by design, public key | No action |
| SEC-5 | INFO | Root repo history contains only the **public** license key (`tools/keys/zarlino_license_public.pem/.txt`, commits `476f1fb1`, `a118ce9d`); **no private-key blocks exist in any commit** (`git log -S"PRIVATE KEY"` → empty) | history scan | Clean |
| SEC-6 | INFO | Private license key material exists ONLY locally: `tools/keys/ztame_license_private.pem`, ignored via `.gitignore:23` (`tools/keys/*private*.pem`); no `*.pem` tracked today | ignore + `git ls-files` | Clean, but local-only (migration source for Phase 3) |
| SEC-7 | INFO | GitHub Actions secrets/vars not enumerable with current token (`ZARLINO_SIGNING_ENABLED`, `AZURE_*`, `CLOUDFLARE_API_TOKEN`, `ZARLINO_LICENSE_PUBLIC_KEY` referenced by workflows) | workflow files | BLOCKED (no admin scope) |

## Notes for the migration

1. The root repo was **already private** — the remaining Phase 2 work is: push/protect the branch state, enable GitHub secret scanning + push protection (admin scope), and prune/handle the local deletion of `executive-os-ci.yml`.
2. No source/binary-relationship metadata exists (0 releases/tags) — Phase 8 must create traceable releases (commit SHA ↔ artifact hash).
3. `ADMIN_TOKEN` must be treated as compromised **now** (tracked + historical) — rotation required regardless of migration.
4. Local `.dev.vars` is the practical source for Phase 3 secret migration (DeepSeek/Paystack/GitHub/founder tokens) — transfer must use machine-to-machine mechanisms (`wrangler secret put` via stdin, GitHub Actions secrets API); no human copying, no echo, no git.
5. `tools/keys/ztame_license_private.pem` (local, ignored) must migrate to the exec-os worker secret + any future signing service, then be removed from this machine at decommission (Phase 14).

## Verification statement (mandate §30)

- ACTION: audit repositories for committed secrets and repo exposure.
- EXPECTED RESULT: confirm visibility state, scan working trees + history for credential material, test whether ANY secret values leaked into VCS.
- ACTUAL RESULT: repo already private; 3 actionable credential findings (SEC-1..3); local-only keys contained (SEC-5/6); history has zero private-key material.
- INDEPENDENT VERIFICATION: GitHub API repo metadata; `git ls-files` / `git log -S` / `git check-ignore`; working-tree pattern scan (576 files, output redacted).
- EVIDENCE: findings table above; commit hashes named; file:line references.
- STATUS: **PARTIALLY VERIFIED** — completeness gap: full-history scanning of every blob and GitHub-side secret scanning are BLOCKED without admin scope / `gh` CLI.
