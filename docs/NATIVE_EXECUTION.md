# Native game execution

The enhanced host's default build uses `SC_PROGRAM=ON`, `SC_AOT=OFF` and
`SC_INTERPRETER_REFERENCE=OFF`. It links `src/sc_cpu_state.c` for the existing
register, memory-bus, flag and saved-state ABI, rather than the 65816
interpreter implementation. A request to interpret an uncovered instruction
is a diagnostic failure, never an implicit fallback.

`tools/compile_native_program.py` emits compact connected C for the US hot
paths and a complete per-address C tier for all 524,288 ROM bytes. Optional
verified Europe, France, Germany and Japan profiles have the same complete
coverage. ROM mirrors retain the actual program bank, fetch address, live
operands, register widths and original ordered memory callbacks. The code
checks its expected opcode; it does not select actions by decoding a live
opcode. RAM and SRAM are not admitted as ROM code.

Two supported host code variants are compiled explicitly: the US view-cursor
store replaced with four NOPs, and the private construction bus's money-HUD
entry replaced with RTL. Both retain their exact fetch/stack/cycle behavior.
US helper substitutions are fingerprint-guarded; foreign cartridges execute
their own compiled code at those addresses.

The host retains its existing game hooks, IRQ/NMI/WAI/STP handling and device
clocks between native edges. Construction, test-city field priming, development
batches and mapped world helpers use the same native entry. Explicit independent
reference routines remain available in oracle builds for comparisons.

The older CpuState AOT/fiber path is separate from this migration. It is not
enabled in the enhanced host because it skips required hooks and uses a
different ABI. `docs/AOT_LLE.md` records its historical bring-up.

## Build and independent controls

Generate the private native sources from your own verified ROM:

```sh
python tools/compile_native_program.py --rom /path/to/us.sfc
```

Repeat `--regional-rom REGION /path/to/regional.sfc` to include additional
verified `eu`, `fr`, `de` or `jp` profiles. Generated code and ROMs stay outside
Git. Regeneration preserves timestamps for unchanged output.

Build the game normally. To link the independent interpreter deliberately,
configure a separate build directory with `-DSC_INTERPRETER_REFERENCE=ON`.
`SC_PROGRAM_REFERENCE=1` and `SC_PROGRAM_CONTROL_REFERENCE=1` in that build
select the independent instruction and control paths. On GNU-compatible
linkers, `SC_REQUIRE_NATIVE=1` guards every interpreter call in a reference
executable, including private CPUs outside the main scheduler.

`UrbanRecompProgramTest ROM` compares compiled actions against the independent
CPU's registers, flags, cycles and ordered bus callbacks. It covers every ROM
byte over 128 state patterns, ROM aliases, architectural BRK, control states
and US connected spans. `UrbanRecompCpuStateTest` compares allocation/reset,
flag handling and saved-state serialization with separately compiled original
core functions. These tests link the oracle; the game does not.

## Release validation

Beta 19 was checked against the following requirements, beyond a zero
main-loop interpreter counter:

- The production link contains the state-only module and no 65816 decoder.
- Complete generated profiles match independent instruction/control oracles.
- Native and original builds match deterministic boot, setup, city, scrolling,
  reports, budget, gift, construction, save and load replays.
- Hidden-city generation and normal accelerated development run without any
  interpreter call, including private CPU work.
- Existing large-map and simulation checks pass, and timing comparisons report
  actual workload and wall-clock limits without conflating them with state parity.

The recorded results are in [performance evidence](GPU_PERFORMANCE.md).
Removing the decoder does not itself establish a general FPS improvement;
most common paths were already compiled in Beta 18.

## Simulation profiling

`SC_PERF=1` separates development batches, population census, power and pixel
work in the frame-time log. `SC_PERF_FRAME_PATH` records per-display timings;
`SC_KERNEL_CLOCK_PATH` optionally records connected simulation entries as CSV
with PC, call count and milliseconds. Per-kernel timers add profiling overhead
and should be disabled for timing comparisons.

`SC_MATH_LANE_REFERENCE=1`, `SC_POWER_LANE_REFERENCE=1` and
`SC_ZERO_CLOCK_REFERENCE=1` independently disable Beta 22's connected arithmetic,
electrical traversal and electrical zero-clock batching additions. They are developer
comparison controls. Deterministic state comparisons disable time-bounded extra
development with `SC_DEVELOPMENT_BATCH_REFERENCE=1`, use the same guest-frame
cohort and compare PPU, CPU, beam, RAM and world data. Dedicated music-worker
state follows wall time. Set `SC_MUSIC_THREAD=0` for longer exact replays that
cross sound-command handshakes, synchronizing SPC execution to the guest clock.
Production timing runs retain the default real-time music worker.
