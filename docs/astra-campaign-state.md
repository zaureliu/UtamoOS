# UTAMO OS — Astra Systems Campaign State

Campaign branch: `astra-campaign`. Local development only; no push, merge into
main, release or public tag is authorized during the campaign.

| Field | Current value |
| --- | --- |
| Current milestone | v0.4.0 — Kernel threads and preemptive scheduler |
| Current status | PLANNING; no v0.4 gate claimed |
| Highest GREEN milestone | v0.3.0 — Kernel Heap |
| Last known good commit | `2fb66be740ed8c153b3d733fc1d16bc978f2364f` |
| Kernel version | 0.3.0; advance only after the next milestone passes |
| Kernel ELF SHA-256 | `e8b103f7632c6fa04186db0b6034d3a223babd5187abe04a91d8c2806d3bcff6` |
| ISO SHA-256 | `67040ab6949cfe343e212a74b9750cd1b03c2f489afb52ea3f5a35e69ed4b111` |
| Host validation | v0.3 final: 18,994 checks, 0 failures |
| ELF validation | v0.3 final: 1,482 checks, 0 failures |
| QEMU validation | v0.3 final: 3,203 checks, 0 failures; 14 sequential VMs reaped |
| Known blockers | None observed at the v0.3 gate |
| Next task | Plan kernel-thread context switching and scheduler contracts against the preserved v0.3 baseline |

## Last known good history

| Milestone | Status | Commit | Evidence |
| --- | --- | --- | --- |
| v0.2.0 | GREEN baseline | `f73951185755da40c85467de99749611f6ab66dc` | `validation-artifacts/astra-baseline-20260914T071530Z/` |
| v0.3.0 | GREEN, 2026-09-14 | `2fb66be740ed8c153b3d733fc1d16bc978f2364f` | `validation-artifacts/astra-v03-final-20260914T074134Z/summary.json` |

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
