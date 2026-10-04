# Five-Group Organisation Audit — roles, tools, skills

**Date:** 2026-10-04 · **Scope:** Product · Growth · Sales · Marketing · Finance (+ Support, found in the sweep)
**Method:** live OS registry (`/api/org/roles`, grants, agents), the 106-tool registry, the skill
folders, and the Doctor register. Everything below was verified against the running system, not
remembered.

---

## 1. Where each group stood before this pass

| Group | Roles (org) | Tools | Skill | Verdict |
|---|---|---|---|---|
| **Product** | `role_product_architect`, `role_product_manager`, `role_software_engineering`, `role_dsp_engineering`, `role_qa_verification`, `role_design_ux` | pipeline(2), projects(5), github(3), review(3), perceptual(14), vision, system(3), release pipeline scripts | 5 DSP/engineering skills | **Covered** |
| **Growth** | `role_growth_intelligence` | marketing(4), youtube(3), web(2), website(2), outreach.report | none | Role + tools, **no skill** |
| **Sales** | `role_sales_partnerships` | outreach(4), contacts(2), paystack(5), finance(4) | none | Role + tools, **no skill** |
| **Marketing** | **none** | marketing(4), website(2), youtube(3) | none | **No role, no skill, no send path** |
| **Finance** | `role_finance_governance` | finance(4), paystack(5), productivity.budget | none | Role + tools, **no skill**; unfundable budget override unguarded |
| **Support** | none (only `role_customer_intelligence` = *analysis*) | **none** | none | **No execution role, no tools, no skill** |

Two structural findings:

1. **Marketing had no role at all.** `role_growth_intelligence` *decides* positioning/campaigns;
   nothing existed to *execute* them (site copy, campaigns, channel analytics, brand voice).
2. **Support had no execution role and no tools.** `role_customer_intelligence` analyses feedback;
   first-line support (inbound mail, complaints, refund intake) had no owner.

---

## 2. What this pass added

### Roles (migration v37, `ORG_ROLES_V37_SQL`)

| New role | Department | Critic | Autonomy | Capability grants |
|---|---|---|---|---|
| `role_marketing_content` — Marketing / Content Agent | `dept_commercial` | `role_growth_intelligence` | L1 | `*` at L1, `spend` L0 (approval), `external` L1 |
| `role_support_operations` — Support Operations Agent | `dept_commercial` | `role_finance_governance` | L1 | `*` at L1, `external` L1 |

Both follow the existing fail-closed convention (wildcard working scope; spend hard-gated;
prompt lineage mirrored from lineage). Org is now **15 roles / 6 departments / 35 capability
grants** (verified live), and the Doctor's registry check passes at the new count.

### Skills (user-level, alongside the 5 engineering skills)

| Skill | Covers | Why it exists |
|---|---|---|
| `zarlino-sales-growth-ops` | sourcing rules, caps, reply/nurture policy, affiliates, funnel metrics | the machine runs unattended — this is the contract it runs under |
| `zarlino-marketing-ops` | consent rules, honest claims, campaign checklist, and the **explicit statement that a bulk sender does not exist yet** | stops an agent improvising a mailing loop |
| `zarlino-support-ops` | triage buckets, refund/dispute intake, privacy, and the **support-queue gap** | support had no written procedure at all |
| `zarlino-finance-ops` | runway discipline, three cost classes, escalation triggers, monthly close | money decisions were being made from memory |

### Tools and guards (this pass)

| Change | Kind | Effect |
|---|---|---|
| `marketing.waitlist_stats` | new tool | Marketing sees real list sizes from KV instead of guessing |
| `executive.cancel_task` | new tool | closes the recorded "no way to retire a stale task" gap; disarms auto-run; reason audited |
| `projects.update_task { autoRun }` | new field | a deliberately parked task can stop pretending to be autonomous work |
| `POST /api/chat/prune` (+ `deleteChatSession`, `listChatSessionsToPrune`) | new route | repeatable context hygiene; dry-run by default; `default` session never touched |
| Doctor `settings.budget_fundability` | new check | WARNs (autoFix clamps **down**) when an AI budget override exceeds the deployed env knob by >2x |
| Doctor `org.task_contracts` | fix | cancelled work no longer counts as "informal conversation" |
| Funnel honesty | fix | `countProspectsByStatus()` excludes self-test rows; report exposes `testProspects` + `countsAll` |

---

## 3. The live cleanup (what was actually removed)

| Item | Before | After |
|---|---|---|
| Chat sessions | **111** | **3** (the founder's main thread + the two most recent) |
| Stale blocked tasks | 6 | 5 cancelled through the governed tool with written reasons; 1 kept (genuine website 404 bug, now with acceptance criteria + QA verifier, auto-run disarmed) |
| Open contradictions | 8 | **0** (7 resolved with evidence, 1 dismissed as historical) |
| Unfunded budget belief | `$100/day` override vs `$5` deploy | **$5/day** — belief equals money |
| Doctor | 26 OK / 3 WARN / 0 FAIL (plus a phantom FAIL on the agent path) | **29 OK / 1 WARN / 0 FAIL** |

The single remaining warning is `perceptual.evidence_gaps` — ZTame/ZScorch need a human listening
test. That is not noise; it is the one gate an AI may not sign for itself.

---

## 4. Remaining gaps per group (honest list)

| Group | Remaining gap | Owner |
|---|---|---|
| Product | Mac/AU builds (no Mac), code-signing certificate, ZScorch/ZVocal test exes "Not Run", version drift (site 1.0.0 vs source 2.0.0) | founder (hardware/cert) + AI (drift, test wiring) |
| Growth | sourcing is still manual-review; campaign content cadence unproven; no second channel | AI (batch 2) + founder (one real customer) |
| Sales | no trial→paid, no receipts/invoicing, single currency (GHS), no distributor | founder (merchant/banking) + AI (invoicing) |
| Marketing | **no bulk campaign sender** (consent + unsubscribe + rate limits + per-recipient audit must exist first); waitlist off by config; no analytics attribution | AI (sender) + founder (go/no-go) |
| Finance | no company bank/books/tax; provider balances funded by hand; refund exposure not tracked as a cost line | founder (entity, bank) + AI (tracking) |
| Support | **no `support.*` tools or queue** for post-sale mail; refund intake is a procedure, not a system | AI (queue) + founder (policy decisions) |
| Cross-cutting | bus factor 1 (one Gmail, one card, one laptop, one human) | founder |

## 5. How to re-run this audit

```powershell
# roles + grants, live
Invoke-RestMethod https://os.zarlinoaudio.com/api/org/roles -Headers $h
# capacity: 15 roles / 6 depts / 33 grants expected
# hygiene metrics
Invoke-RestMethod https://os.zarlinoaudio.com/api/productivity/contradictions -Headers $h
Invoke-RestMethod https://os.zarlinoaudio.com/api/doctor/run -Method Post -Headers $h -Body '{}'
```

A clean system is **29 OK / 1 WARN / 0 FAIL** with **0 open contradictions** and **0 untitled
sessions older than 14 days**. Anything else is either new work or new noise — decide which.
