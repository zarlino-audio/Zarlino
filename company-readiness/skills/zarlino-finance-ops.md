---
name: zarlino-finance-ops
description: Manage Zarlino Audio's money discipline — runway, burn, revenue recording, vendor costs, AI budget, refund exposure. Use this whenever the user asks about money: how much runway is left, what we spend, whether something is affordable, recording revenue/expense, funding DeepSeek or any vendor, or pricing decisions. Trigger for "runway", "burn", "budget", "can we afford", "revenue", "expenses", "top up", "pricing", or any question whose answer depends on cash.
---

# Zarlino Finance Operations

Finance is the company's **veto layer**: growth may want to spend, product may want time — finance
answers with numbers or refuses. Never with optimism.

## 1. The two questions that decide everything

1. **What does it cost, and from which balance?** (a vendor, a card, a provider credit)
2. **What does it return, and when?** If the answer is "trust", it is not a return.

If the funding source is a provider balance (e.g. DeepSeek credit) rather than the bank, the cost
is real but invisible on a bank statement — that is exactly how a company dies mid-quarter.

## 2. Known state (verify, don't trust memory)

- AI budgets: env knobs `AI_DAILY_BUDGET` / `AI_MONTHLY_BUDGET` plus optional store overrides.
  The Doctor now WARNs (`settings.budget_fundability`) when an override exceeds the deployed knob —
  a belief in money that the deploy never authorised.
- Provider balance and a low-balance sentinel (< $10) are checked by the 15-minute monitor; the
  alert is not a top-up.
- Revenue arrives through Paystack; licence issuance lives on the website worker.
- Vendor + card register: `company-readiness/VENDOR_AND_CARD_REGISTER.md` (in the plugin repo).

## 3. Rules

- **Record it or it did not happen.** `finance.record_revenue` / `finance.record_expense` — every
  figure in a report must trace to a row or a provider API response (`paystack.*`).
- **Never quote a runway you have not computed** from the current cash and the current monthly
  burn. `finance.calculate_runway` exists precisely so this is arithmetic, not vibes.
- **Three cost classes, three answers:** subscriptions (do we still use it?), per-use credits (what
  is the real rate per task?), one-offs (what breaks if we skip it?).
- **Refunds are a cost line, not a favour.** Unanswered disputes turn into restricted payment
  accounts — a finance problem, not a support problem.
- **FX and access:** the merchant account is GHS-only. USD pricing or a second channel is a
  strategic finance decision, not a copy tweak.

## 4. Escalate (never decide alone)

Any new recurring cost · any change to AI budgets · taking on debt or a card · moving money to a
new account · pricing changes · anything where the answer requires knowing the founder's personal
finances.

## 5. Monthly discipline

1. Close the books: revenue, expenses, provider balances, outstanding refunds.
2. Recompute projected runway under the **current** burn (not the average of a lucky month).
3. Name the top three costs and ask of each: what did it buy us last month?
4. Report as numbers with dates, not adjectives. If a number is unknown, write "unknown" — that is
   a finding, not a failure.
