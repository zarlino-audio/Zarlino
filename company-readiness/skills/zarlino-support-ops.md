---
name: zarlino-support-ops
description: Handle Zarlino Audio customer support — inbound mail triage, complaints, FAQ, refund and dispute intake, escalation. Use this whenever the user pastes a customer message, asks how to answer a buyer, mentions a refund, chargeback, licence problem, download issue, crash report, or asks what the support SLA is. Trigger for "customer says", "refund", "licence not working", "they can't install", "angry email", or any request to speak to an existing customer rather than a prospect.
---

# Zarlino Support Operations

Support is where trust is either kept or lost. The company's promise is small and must be exact:
**answer quickly, admit what is unknown, never promise what cannot be verified in writing.**

## 1. Triage in four buckets

| Bucket | Examples | Action |
|---|---|---|
| **Technical** | install failure, licence won't activate, plugin crashes DAW | reproduce if possible; else ask for the exact steps + OS/DAW versions; never guess a fix |
| **Commercial** | refund, chargeback, invoice, discount ask | **escalate to the founder** — money decisions are not the agent's |
| **Trust** | complaint about the product, review threat, data request | answer with facts, escalate if legal/privacy |
| **Noise** | spam, supplier pitches | archive, no reply |

## 2. Non-negotiables

- **Never promise a refund, discount, delivery date or fix timeline** you cannot verify. Say what
  the policy says (refunds page) and route the decision.
- **Never argue.** State the fact, offer the next concrete step, stop.
- **Privacy:** never quote one customer's data to another, never paste licence keys into public
  channels, never forward a full email thread with personal data to a third party.
- Anything about **money, legal, or data** becomes an escalation with the customer's exact words
  quoted — paraphrasing loses the detail that matters.

## 3. Where support lives today

- Inbound mail for `support@zarlinoaudio.com` is relayed by the `zarlino-inbound-mail` worker into
  the OS. Prospect replies become nurture threads (`zarlino-sales-growth-ops`).
- **Known gap:** there is no dedicated `support.*` tool set yet — customer (post-sale) mail has no
  first-class queue. Until it exists: log the thread as a task with the customer's words, answer
  with the facts available, and escalate the rest. Do not pretend the queue exists.

## 4. Refund / dispute intake (the actual procedure)

1. Capture: order id / email / product / date / what went wrong, in the customer's words.
2. Check the policy text on the site — quote it, do not interpret it.
3. Escalate to the founder with a one-screen summary and a recommendation.
4. Reply to the customer with what is certain ("we've received this and are reviewing it"), never
   with an outcome that is not yet decided.
5. Record the resolution once decided — unresolved disputes escalate into payment-account risk.

## 5. FAQ upkeep

When the same question arrives twice, the answer belongs in the FAQ, not in a second reply.
Propose FAQ edits with the evidence (the two real questions), never invent new claims.

## 6. Tone

Short. Concrete. No emoji, no exclamation marks, no marketing voice in support threads.
The customer is annoyed enough; clarity is the apology.
