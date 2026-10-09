ZARLINO CLOUD-ONLY MCP MIGRATION

Full Implementation Mandate

You are the implementation AI responsible for migrating Zarlino Audio from a locally dependent operating environment into a cloud-native, MCP-controlled operating system.

This is not a prototype.

The target is a production-capable architecture in which Claude, ChatGPT, or any other MCP-compatible AI can independently operate Zarlino's technical and operational infrastructure without access to the founder's personal computer.

The founder's local machine will have ONE final interaction only: execute the final export/migration task required to transfer remaining reports, assets, and state into the cloud/GitHub system. After that, the local machine is no longer part of Zarlino's operating architecture.

---

1. NON-NEGOTIABLE END STATE

The final architecture must be:

             Claude / ChatGPT / Other MCP AI
                         │
                         ▼
                 ZARLINO MCP API
                  / Control Plane
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
   Cloudflare         GitHub            Azure
   Runtime/State      Source/CI         Compute
        │                │                │
   KV / DO / R2     Actions / Repo    VM / Container
        │                │                │
        └────────────────┼────────────────┘
                         │
                         ▼
                 Zarlino State /
                 Artifacts /
                 Results
                         │
                         ▼
                  CEO Console
              READ + APPROVAL ONLY

Responsibilities

MCP

- AI control plane
- authentication
- authorization
- tool interface
- job creation
- job monitoring
- state access
- approval workflow
- audit trail

Cloudflare

- MCP/API runtime
- lightweight services
- durable state
- KV
- Durable Objects where required
- R2 where appropriate
- scheduled triggers
- web-facing infrastructure

GitHub

- source-code authority
- issues
- pull requests
- branches
- tags
- GitHub Actions
- build/test pipeline
- release metadata

Azure

- heavy compute
- long-running workloads
- C++/JUCE builds where appropriate
- headless audio rendering
- batch DSP processing
- CPU-intensive analysis
- containerized workers
- optional VM-based execution
- Azure Key Vault if it is the appropriate secure secret store

R2 / Azure Blob

- large binaries
- WAV files
- render outputs
- reports
- packaged releases
- other large artifacts

CEO Console

- primarily read-only
- approvals for exceptional/high-risk operations
- no routine operational dependency

Local computer

- NOT part of the production architecture
- NOT a fallback execution environment
- NOT a required build machine
- NOT a required rendering machine
- NOT a required deployment machine

---

2. PRIMARY ARCHITECTURAL PRINCIPLE

The system must follow:

«CEO governs. AI operates. MCP controls. Cloud infrastructure executes. GitHub records code truth. Cloud storage records artifacts. Every meaningful operation is durable, auditable, and independently verifiable.»

Do not create an architecture where the AI merely tells the founder what commands to run.

The AI must be able to execute permitted operations through MCP.

---

3. CLOUD PROVIDER STRATEGY

Do not force every workload onto one provider.

Select the cheapest reliable service appropriate for each workload.

Default division

Workload| Preferred substrate
MCP HTTP API| Cloudflare Workers
Small API operations| Cloudflare Workers
Durable application state| Cloudflare KV / Durable Objects
Large artifacts| Cloudflare R2 and/or Azure Blob
Source control| GitHub
CI/CD| GitHub Actions
Normal builds| GitHub Actions
Heavy C++ builds| GitHub Actions and/or Azure
Headless audio rendering| Azure compute
Batch DSP analysis| Azure compute
Long-running processes| Azure
Secrets| Cloudflare Secrets and/or Azure Key Vault
Monitoring| Cloudflare + Azure Monitor where applicable
Scheduled lightweight tasks| Cloudflare Cron
Heavy scheduled jobs| Azure Jobs / Functions / Containers
Releases| GitHub Releases
AI provider| Existing provider/OpenRouter architecture

Do not add infrastructure simply because it is available.

Every additional service must have a documented reason.

---

4. AZURE REQUIREMENT

Azure is available and must be treated as a first-class execution substrate.

Evaluate Azure for:

1. JUCE/C++ compilation
2. Windows plugin builds
3. headless audio rendering
4. CPU-intensive DSP analysis
5. batch perceptual stimulus generation
6. long-running agents
7. containerized workers
8. artifact processing
9. scheduled heavy workloads

Where an Azure VM is required, it should be ephemeral or automatically managed wherever practical.

The system should:

create job
→ provision/use compute
→ execute
→ upload result
→ persist result
→ terminate/release compute

Do not leave expensive compute running unnecessarily.

The implementation must record:

- provider
- resource type
- region
- workload
- estimated/actual cost where available
- lifecycle
- cleanup policy

---

5. SECURITY — ZERO HUMAN SECRET HANDOFF

The migration must require NO manual secret copying or re-entry wherever technically possible.

Never:

- print secrets into logs
- commit secrets to Git
- put secrets in source files
- expose private keys through MCP
- expose secret values to the CEO console
- include credentials in reports
- place credentials inside generated prompts
- store plaintext secrets in GitHub

The system must create a secret inventory:

Secret| Purpose| Current location| Destination| Status

Secret values must never appear in this inventory.

Use secure machine-to-machine migration mechanisms.

Possible destinations include:

- Cloudflare Secrets
- GitHub Actions Secrets
- Azure Key Vault
- other appropriate encrypted secret stores

The implementation AI must determine the safest practical mechanism.

---

6. GITHUB

GitHub becomes the source-of-truth for Zarlino code.

Perform an inventory of:

- repositories
- visibility
- branches
- tags
- releases
- Actions
- secrets
- workflows
- issues
- pull requests
- assets
- source/binary relationships

The current public Zarlino repository containing sensitive material must be transitioned to private.

Preserve history and necessary assets.

Audit the repository for:

- secrets
- private keys
- credentials
- internal endpoints
- obsolete infrastructure
- dead URLs
- local-machine paths
- stale binaries
- source/binary drift

Do not claim the repository is secure merely because it has been made private.

Perform an actual secret scan.

---

7. CLOUDFLARE

Audit and configure:

- Workers
- KV
- Durable Objects
- R2
- routes
- environment variables
- secrets
- cron triggers
- bindings
- production deployment
- authentication
- authorization

ZARLINO_KV

If ZARLINO_KV exists but is unbound:

1. bind it
2. deploy
3. perform write
4. perform read
5. perform read-after-write verification
6. verify through the production route
7. record evidence

Do not mark it fixed based only on configuration files.

---

8. MCP CONTROL PLANE

Build or refactor the Zarlino MCP interface so that any compliant AI client can operate the system.

Avoid vendor-specific assumptions.

The MCP layer must expose read and write operations.

Read tools

Implement or preserve equivalent tools for:

- company state
- finance snapshot
- product state
- product pipeline
- product gates
- reviews
- perceptual tests
- perceptual evaluators
- incidents
- approvals
- decisions
- contradictions
- reminders
- tasks
- GitHub state
- deployment state
- cloud health
- audit log
- artifact inventory
- job status
- release status

Reuse existing names where they already exist rather than unnecessarily creating duplicates.

---

9. MCP WRITE OPERATIONS

The AI must be able to perform permitted operations such as:

- create task
- update task
- create decision
- record decision
- create note
- create incident
- close incident
- create review
- create perceptual test
- create evaluator
- create enrollment token
- generate enrollment link
- record evaluator response
- create GitHub issue
- create branch
- create commit
- create pull request
- dispatch build
- dispatch test
- dispatch render
- upload artifact
- create release
- deploy
- rollback

All operations must be authenticated and authorized server-side.

---

10. RISK MODEL

Every write operation must have a risk class:

READ
LOW
MEDIUM
HIGH
CRITICAL

Example:

READ

No approval.

LOW

Routine state updates.

MEDIUM

Operational changes with limited blast radius.

HIGH

Production deployment, major configuration changes, release operations.

CRITICAL

Security-sensitive changes, destructive actions, financial actions, irreversible operations.

The AI must never bypass the approval system.

For approval-required operations:

AI requests operation
        ↓
Approval created
        ↓
CEO approves
        ↓
Operation executes
        ↓
Result verified
        ↓
Audit entry created

---

11. DURABLE JOB SYSTEM

This is one of the most important requirements.

Previous local execution exposed a critical failure mode where a system could report a task ID even though the job later could not be found.

The new architecture must make this impossible.

Every asynchronous operation must follow:

REQUESTED
   ↓
PERSISTED
   ↓
QUEUED
   ↓
CLAIMED
   ↓
RUNNING
   ↓
SUCCEEDED / FAILED / CANCELLED

A job must not be described as "successfully dispatched" until the durable job record has been independently verified.

The system must persist:

- job ID
- operation
- requester
- timestamp
- parameters
- risk class
- approval ID if applicable
- execution provider
- worker/resource
- start time
- end time
- status
- stdout/stderr references
- artifacts
- error
- verification result

If execution crashes or disappears, the system must clearly report:

FAILED / INTERRUPTED / UNKNOWN

—not pretend that the operation succeeded.

---

12. CLOUD EXECUTION

The generic execution architecture should be:

AI
 ↓
MCP
 ↓
Durable Job
 ↓
Execution Provider
 ↓
Worker
 ↓
Artifact Storage
 ↓
Result Persistence
 ↓
Independent Verification
 ↓
MCP

The execution provider may be:

- GitHub Actions
- Cloudflare
- Azure
- another explicitly approved cloud service

The provider must be selected according to workload.

---

13. BUILD SYSTEM

Create a cloud-native build pipeline for Zarlino plugins.

At minimum:

checkout source
↓
install dependencies
↓
configure build
↓
compile
↓
run unit tests
↓
run DSP validation
↓
validate plugin structure
↓
package
↓
checksum
↓
upload artifact
↓
record metadata

Record:

- commit SHA
- repository
- branch/tag
- compiler
- operating system
- architecture
- build configuration
- timestamp
- artifact hash
- tests
- warnings/errors
- build environment

The final artifact must be traceable to source.

---

14. AUDIO RENDERING

Replace all local rendering dependencies.

Cloud rendering must support:

- WAV input
- plugin processing
- dry output
- wet output
- loudness analysis
- peak analysis
- metadata
- artifact upload

Preferred architecture:

Input WAV
   ↓
Render Job
   ↓
Azure compute
   ↓
Plugin / DSP
   ↓
Dry + Wet WAV
   ↓
Audio analysis
   ↓
R2 / Azure Blob
   ↓
Perceptual Test

A DAW and physical audio interface must not be required for offline validation.

Physical recording is a different problem; once source recordings exist, their processing must be cloud-executable.

---

15. ZTAME PERCEPTUAL VALIDATION

Implement a proper evidence pipeline.

Use multiple distinct source types such as:

- vocal
- guitar
- bass
- drums

For each source:

- dry stimulus
- processed stimulus
- level matching
- blind A/B
- randomized presentation
- response capture
- evaluator identity/token
- timestamp
- test version
- stimulus provenance

Critical principle:

«A render is a stimulus, not evidence.»

Evidence exists only after valid human evaluator responses have been collected.

The sequence must be:

Generate stimuli
↓
Create perceptual test
↓
Create evaluator enrollment route
↓
Enroll evaluators
↓
Collect responses
↓
Validate responses
↓
Analyze
↓
Generate evidence
↓
Apply product gate

---

16. EXTERNAL HUMAN EVALUATOR ENROLLMENT

Build a real enrollment mechanism.

It must support:

- unique evaluator token
- persistent token
- evaluator/test association
- expiration
- revocation
- blinded identity where appropriate
- response persistence
- test association
- valid URL
- route verification

When generating an enrollment link, independently verify:

1. token exists
2. token is stored
3. route exists
4. URL resolves
5. evaluator can reach the test
6. test ID is correct
7. test is active
8. response submission works

Only then report the enrollment link as verified.

---

17. CURRENT ZTAME V1.1 VALIDATION

Preserve and integrate the existing ZTame v1.1 validation framework where applicable.

The current concept is approximately:

- 8 stimuli
- 4 source categories
- dry/wet comparison
- blind/randomized presentation
- level matching around -23 LUFS
- target of 32 accepted responses
- 8 responses per source/pair
- majority preference criterion
- no source significantly preferring against the product

Do not silently alter existing acceptance criteria.

If existing records differ from the specification, surface the contradiction.

---

18. CLOUD DOCTOR

Build a Cloud Doctor capable of independently testing:

MCP

- authentication
- authorization
- tool availability
- read operations
- write operations

Cloudflare

- Worker
- KV
- Durable Objects
- R2
- routes
- secrets
- production bindings

GitHub

- authentication
- repository access
- Actions
- workflow execution
- artifact retrieval
- release creation

Azure

- authentication
- compute availability
- job execution
- artifact upload
- cleanup
- monitoring

Execution

- job persistence
- job claiming
- execution
- result persistence
- artifact persistence
- retry behavior

Perceptual

- stimulus creation
- test creation
- enrollment
- response capture
- evidence generation

Governance

- approval
- authorization
- audit logging

Doctor output must distinguish:

TOOL BROKEN
SERVICE DEGRADED
CONFIGURATION ERROR
EXTERNAL PROVIDER FAILURE
BUSINESS ISSUE
AUTHORIZATION FAILURE
DATA ISSUE
VERIFIED

Do not collapse all failures into "system error."

---

19. EXISTING INCIDENTS

Audit known historical issues rather than assuming migration solves them.

Relevant examples include:

- local task pipe producing unreliable/phantom task IDs
- missing ZARLINO_LICENSE_PRIVATE_KEY
- unbound ZARLINO_KV
- agent-ready registry issues
- interrupted jobs
- stale local dependencies
- source/binary drift
- broken plugin URLs
- false license-delivery claims
- website collection/dashboard errors
- deployment inconsistencies

For every issue determine:

Issue
Cause
Still relevant?
Migration impact
Fix
Verification

If an issue becomes irrelevant because the underlying local architecture is eliminated, explicitly document that.

---

20. LICENSE SYSTEM

Audit the licensing architecture.

Verify:

- private key handling
- license generation
- license validation
- product identity
- installation
- download flow
- purchase flow
- Paystack integration
- post-payment behavior
- plugin activation
- offline behavior where applicable

Do not claim "license delivery" unless the system actually delivers the license through the documented mechanism.

---

21. PAYSTACK / PAYMENTS

Preserve existing payment infrastructure unless there is a demonstrated reason to change it.

Verify:

- checkout
- callback
- payment verification
- transaction state
- license generation
- download authorization
- failure handling
- currency behavior
- international purchase assumptions

Document any current geographic/currency constraints.

Do not silently change commercial behavior during infrastructure migration.

---

22. CEO CONSOLE

Reduce the CEO console to a governance interface.

It should show:

- company state
- cash/finance snapshot
- product state
- pipeline
- incidents
- evidence
- open decisions
- contradictions
- deployments
- jobs
- cloud health
- releases
- approvals
- audit log

The console should primarily be:

OBSERVE → UNDERSTAND → APPROVE

Routine operations should happen through MCP.

---

23. AI INTEROPERABILITY

The system must not assume a single AI provider.

The same MCP server should be usable by:

- ChatGPT
- Claude
- other MCP-compatible AI clients

AI-specific prompting should not contain hardcoded assumptions that make another MCP client incapable of operating the system.

The AI client should be replaceable without rebuilding the Zarlino infrastructure.

---

24. OBSERVABILITY

Every meaningful operation should leave an audit trail.

Record:

- who/what initiated it
- AI client if known
- operation
- parameters/reference
- approval
- execution provider
- result
- artifacts
- timestamp
- verification
- errors

Do not store secrets in logs.

---

25. ARTIFACT MANAGEMENT

Create a unified artifact inventory.

Each artifact should have:

- artifact ID
- type
- source job
- source commit
- version
- storage location
- checksum
- creation time
- status
- retention policy

Examples:

- plugin binaries
- WAV renders
- test stimuli
- reports
- datasets
- logs
- releases
- documentation

Use R2 and/or Azure Blob based on cost and workload.

Do not duplicate large artifacts unnecessarily.

---

26. MIGRATION ORDER

Execute in this order:

Phase 1 — Inventory

Map the entire existing system.

Phase 2 — Repository preservation

Audit GitHub and make sensitive repositories private.

Phase 3 — Secrets

Migrate secrets securely without human copying.

Phase 4 — Cloud state

Repair/configure Cloudflare state, KV, DO, R2.

Phase 5 — MCP read plane

Make all important company/system state accessible.

Phase 6 — MCP write plane

Implement authenticated, authorized operations.

Phase 7 — Durable jobs

Eliminate phantom/non-persistent execution.

Phase 8 — GitHub cloud builds

Build plugins without the local machine.

Phase 9 — Azure execution

Implement heavy compute and headless rendering.

Phase 10 — Artifact system

Centralize large cloud artifacts.

Phase 11 — Perceptual validation

Implement stimuli → enrollment → responses → evidence.

Phase 12 — CEO console

Reduce it to observation and approval.

Phase 13 — Independent verification

Run Cloud Doctor against the complete architecture.

Phase 14 — Local decommission

Only after the cloud architecture independently proves it can operate.

---

27. FINAL LOCAL MACHINE TASK

The founder's machine gets exactly ONE final interaction.

That task should:

1. collect required reports
2. collect remaining required assets
3. collect relevant state/configuration
4. export to the designated cloud/GitHub destination
5. verify successful transfer
6. report what was transferred
7. stop

The task must not require the founder to manually:

- copy secrets
- paste credentials
- edit configuration
- upload individual files
- diagnose infrastructure
- run multiple commands
- repeat failed operations

After successful migration:

«The local machine is no longer an operational dependency.»

Do not use the local machine as a fallback after this point.

---

28. COST DISCIPLINE

The architecture should be designed around:

- existing Cloudflare infrastructure
- GitHub Free where technically sufficient
- available Azure resources/credits where applicable
- minimum additional recurring spend

Before adding paid infrastructure, determine whether the workload can be handled by:

1. GitHub Actions
2. Cloudflare
3. Azure
4. existing infrastructure

Use ephemeral compute for expensive workloads wherever practical.

Do not optimize prematurely, but do not create permanent expensive infrastructure for occasional workloads.

---

29. ACCEPTANCE TEST

The migration is complete only when an independent MCP-compatible AI can perform the following without the founder's local machine:

Read

- current company state
- current product state
- open incidents
- product blockers
- financial snapshot
- current evidence
- job state
- cloud health

Execute

- create task
- create GitHub issue
- initiate build
- initiate test
- initiate audio render
- upload artifact
- create perceptual test
- create evaluator enrollment
- retrieve verified enrollment link
- inspect job result

Governance

For an approval-required operation:

request
→ approval
→ CEO approval
→ execution
→ verification
→ audit

Every step must be observable.

---

30. VERIFICATION STANDARD

Never report vague completion.

Every significant operation must be reported as:

ACTION:
What was attempted.

EXPECTED RESULT:
What should happen.

ACTUAL RESULT:
What happened.

INDEPENDENT VERIFICATION:
How it was verified.

EVIDENCE:
IDs, hashes, URLs, logs, artifacts, or other concrete evidence.

STATUS:
VERIFIED
PARTIALLY VERIFIED
BLOCKED
FAILED
NOT APPLICABLE

Do not use "done" as a substitute for evidence.

Do not claim a job was successful merely because a request was accepted.

Do not claim a deployment succeeded merely because a deployment command returned.

Do not claim a perceptual test produced evidence merely because stimuli were rendered.

---

31. REQUIRED DELIVERABLES

Produce:

1. final architecture document
2. complete infrastructure inventory
3. security/secret migration report
4. GitHub audit report
5. Cloudflare configuration report
6. Azure execution architecture
7. MCP tool catalog
8. MCP authorization policy
9. durable job specification
10. GitHub Actions build pipeline
11. cloud audio-render pipeline
12. artifact-storage architecture
13. evaluator enrollment system
14. perceptual-validation integration
15. Cloud Doctor
16. incident disposition report
17. CEO-console architecture
18. migration verification report
19. rollback plan
20. local-machine decommission report
21. final operating runbook

---

32. FAILURE HANDLING

If something cannot be completed:

Do not hide it.

Report:

BLOCKED:
Reason

DEPENDENCY:
What is missing

CURRENT STATE:
What actually exists

NEXT ACTION:
What would resolve it

RISK:
What remains exposed

If an external provider fails, distinguish that from Zarlino's own infrastructure failure.

If a tool is unavailable, do not fabricate its output.

If a job cannot be verified, mark it unverified.

---

33. ARCHITECTURAL SUCCESS CONDITION

The final test is simple:

«If the founder's laptop disappeared immediately after migration, could Zarlino still build plugins, render audio, run tests, store artifacts, manage state, create releases, inspect infrastructure, conduct perceptual validation, and be operated by an MCP-compatible AI?»

The required answer is:

YES.

If the answer is not yet yes, migration is not complete.

---

FINAL OPERATING PRINCIPLE

Build Zarlino so that:

THE FOUNDER
    ↓
sets strategy
approves exceptional actions
reviews evidence
    ↓
AI
    ↓
operates through MCP
    ↓
MCP
    ↓
enforces permissions
persists jobs
records decisions
    ↓
CLOUDFLARE
    ↓
runs control-plane services
    ↓
GITHUB
    ↓
owns source + CI/CD
    ↓
AZURE
    ↓
executes heavy computation
    ↓
R2 / AZURE BLOB
    ↓
stores artifacts

The local computer is removed from the production loop.

Do not build another system that merely moves the same dependency from one machine to another.

Build the cloud operating system.