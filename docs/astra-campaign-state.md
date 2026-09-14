# UTAMO OS — Astra Systems Campaign State

Campaign branch: `astra-campaign`. Local development only; no push, merge into
main, release or public tag is authorized during the campaign.

| Field | Current value |
| --- | --- |
| Current milestone | v0.6.0 — VFS, initramfs and ELF userspace |
| Current status | PLANNING; implementation has not started at this checkpoint |
| Highest GREEN milestone | v0.5.0 — Isolated Ring 3 processes and native syscalls |
| Last known good commit | `4f303c8aeca26051f4affe92b0ba5af4e2296026` |
| Kernel version | 0.5.0; advance only after the next milestone passes |
| Last known good ELF SHA-256 | `58a6bdfff0b0408f37eff0033956f8449480cddfe60b23a14d6eefcec96a4b82` |
| Last known good ISO SHA-256 | `88f2a20abca8d27f2df682f9700cf7b7638b15beebe4a1a779a976df95004607` |
| Host validation | v0.5 final: 25,541 checks, 0 failures |
| ELF validation | v0.5 final: 1,707 checks, 0 failures |
| QEMU validation | 10,257 candidate + 3,115 final-stamp checks; 22 sequential VMs reaped; 0 failures |
| Known blockers | None observed at the v0.5 gate |
| Next task | Plan VFS/initramfs, a bounded ELF loader and userspace runtime on the preserved v0.5 base |

## Last known good history

| Milestone | Status | Commit | Evidence |
| --- | --- | --- | --- |
| v0.2.0 | GREEN baseline | `f73951185755da40c85467de99749611f6ab66dc` | `validation-artifacts/astra-baseline-20260914T071530Z/` |
| v0.3.0 | GREEN, 2026-09-14 | `2fb66be740ed8c153b3d733fc1d16bc978f2364f` | `validation-artifacts/astra-v03-final-20260914T074134Z/summary.json` |
| v0.4.0 | GREEN, 2026-09-14 | `7e07719f2207fca29de4c7bafcaa296d2dd30b8a` | `validation-artifacts/astra-v04-final-20260914T084853Z/summary.json` |
| v0.5.0 | GREEN, 2026-09-14 | `4f303c8aeca26051f4affe92b0ba5af4e2296026` | `validation-artifacts/astra-v05-final-20260914T101644Z/summary.json` |

Baseline was rebuilt from a clean build directory before campaign edits:
host, cross kernel, ELF/ABI, ISO and the full existing headless matrix passed.
Counts describe these runs, not future builds. Frozen artifacts are kept
locally in `validation-artifacts/astra-last-known-good/v0.2.0/`.

The final v0.3 clean build, host suite, ELF/ABI inspection, ISO and headless
matrix passed: **23,679 checks, zero failures**. Four heap suites exercised
64/256/512 MiB and a 64 MiB CPU without NX, with three 8,192-operation stress
runs each: **98,304 kernel stress operations**. Memory, shell and exception
regressions also passed. All 14 VMs were reaped. These are recorded assertions,
not unique-test or coverage counts. See [the v0.3 record](validation-astra-v0.3.json).
The frozen v0.3 copy is `validation-artifacts/astra-last-known-good/v0.3.0/`.

## Campaign gates

Implement → host tests → kernel build → ELF/ABI → ISO → headless QEMU →
stress/negative tests → audit → commit → last known good → next milestone.

A failed dependent gate stops advancement. Record its cause, attempts and safe
next action here; never replace an earlier GREEN entry with an incomplete one.
Version/changelog advance only after evidence supports a GREEN milestone.

Priority: v0.3 heap; v0.4 kernel threads/preemptive scheduler; v0.5 isolated
Ring 3 processes/syscalls; v0.6 VFS/initramfs/ELF userspace. Storage v0.7 and
networking v0.8 begin only after all prerequisites pass. No GUI, SMP, USB or audio.

## Preserved references and constraints

- `main` / `origin/main`: `109fe33fa839a585861a135a85d60ddda9428c92`.
- `v0.1.0` tag object: `0187e97b34e23c78671b317b71b1e3090d76506d`.
- `v0.0.1`: `273e47634b113497cfb667a4c9de602420634e2c`.
- `v0.2-dev` remains at the baseline commit above.
- Existing cross compiler, Binutils, NASM and Limine are reused without updates.
- All QEMU runs are headless, bounded, sequential and explicitly reaped.
- Manual/visual, UEFI and physical hardware acceptance remain separate evidence.


## v0.4 gate evidence

Kernel threads, guarded stacks, round-robin preemption, sleep/wakeup, deferred
reaping and shell diagnostics passed the local gate. The final clean build
passed 22,794 host and 1,563 ELF/ABI checks and produced the 0.4.0 ELF/ISO.

Evidence has two explicit phases. The full candidate matrix, still stamped
0.3.0, passed 6,081 QEMU checks across 15 VMs: scheduler suites at 64/256/512 MiB
and without NX, allocator/shell regressions and exception probes. After the
version stamp, 0.4.0 passed another 1,243 checks across three VMs: boot,
scheduler suite and the stack-guard fault. All 18 VMs passed and were reaped.

Binary comparison found identical `.text`, `.data` and `.limine_requests`;
`.rodata` differs by the one version byte. Counts total **31,681 assertions,
zero failures**, counting final host/ELF once plus both QEMU phases. They are
not unique-test or coverage counts. See [the v0.4 record](validation-astra-v0.4.json).
Frozen artifacts: `validation-artifacts/astra-last-known-good/v0.4.0/`.

During review, thread-name validation moved into its IF-protected copy
transaction; fatal console output stayed independent of scheduler integrity.
Only the new stack fixtures aggregated repeated callback/page observations:
137,325 became 1,096 assertions while preserving cases, pages and bytes.
No v0.3 suite or check was removed. Raw preliminary logs remain under
`validation-artifacts/astra-v04-incremental/` and
`validation-artifacts/thread-stack-agent/`.

## v0.5 gate evidence

Private user address spaces, TSS/CPL3 entry, native INT128 syscalls and
contained user faults passed the local gate. The final clean build passed
25,541 host and 1,707 ELF/ABI assertions and produced the 0.5.0 ELF/ISO.

The full candidate matrix, stamped 0.4.0, passed 10,257 QEMU checks across
19 VMs, including process suites at 64/256/512 MiB, NX-off refusal, kernel
allocator/scheduler/shell regressions and fatal kernel probes. The final
0.5.0 stamp passed 3,115 more checks in three VMs: process suite (2,286),
NX-off suite (783) and the original kernel page fault (46). All 22 VMs
passed and were reaped. The full matrix was not repeated after stamping.

Comparison found identical `.text`, `.data` and `.limine_requests`;
`.rodata` differs by one version byte. Final host/ELF counted once plus
both QEMU phases total **40,620 assertions, zero failures**. The 12 usertest
runs created and reaped 480 processes, contained 156 user faults and completed
3,934,800 syscalls. These counts describe recorded assertions and operations,
not unique tests or coverage. [The v0.5 record](validation-astra-v0.5.json)
identifies the hashes, phases and supplemental UBSan checks excluded from
the total. Summary: `validation-artifacts/astra-v05-final-20260914T101644Z/summary.json`.

Audit fixed IRQ wake/accounting on invalid user return state, effective
higher-half permissions in user fault diagnostics and wait-deadline overflow.
The GDB observer now reads architectural RFL from its same paused monitor
snapshot; the original cast failure and its offline replay remain historical
evidence, outside the passing matrix totals. Earlier GREEN entries are preserved.
