# First playable: peer-project findings

Research date: 2026-09-21. This is an implementation investigation, not a claim that a WDC build has reached gameplay.

## Decisive difference from the sibling ports

WDC uses BOSS ZSort, a substantial variation of Nintendo ZSort rather than the F3D family. Its microcode combines graphics and audio, fits within 1024 instructions, and requires CPU/RSP signal interaction through SP_STATUS. GLideN64's author reports successful support in mupen64plus after corresponding core changes. Consequently, copying a sibling's graphics-task dispatch and separate audio microcode is insufficient. [Primary implementation report](https://gliden64.blogspot.com/2019/02/hle-implementation-of-boss-zsort.html).

The implementation makes the handshake concrete: EndMainDL waits for SIG0 (0x80); WaitSignal clears SIG1/SIG2, sets SIG3 (0x400), and waits for the CPU to clear it. It also has audio commands, RDRAM/DMEM transfers, matrix operations, screen-space objects, and custom RDP state commands. [GLideN64 ZSortBOSS source](https://github.com/gonetz/GLideN64/blob/master/src/uCodes/ZSortBOSS.cpp). This is a valuable protocol reference; copying its code requires respecting its [GPLv2 license](https://github.com/gonetz/GLideN64/blob/master/LICENSE).

## Existing renderer route

The inspected WDC dependency checkout at `lib/rt64` has no ZSort/BOSS entry in `src/gbi/rt64_gbi.h`. It does already expose `Application::processDisplayLists(memory, start, end, false)`: the false argument selects `Interpreter::processRDPLists`, including raw triangle processing and partial-command buffering. Automatic SP completion is emitted only for the HLE path. The raw RDP path is therefore a plausible sink for commands produced by an independently executed BOSS RSP, without first implementing a BOSS HLE renderer. [Application source](https://github.com/rt64/rt64/blob/main/src/hle/rt64_application.cpp), [interpreter source](https://github.com/rt64/rt64/blob/main/src/hle/rt64_interpreter.cpp). Local source inspection is authoritative for these exact signatures.

This is an engineering inference, not demonstrated WDC compatibility. DPC start/end writes, memory byte order, DP completion, and VI presentation still need integration and verification.

## Recompiler limitations that matter

The sibling's local `lib/N64ModernRuntime/N64Recomp/RSPRecomp/src/rsp_recomp.cpp`, functions `expected_c0_reg_value` and `c0_reg_write_action`, hardcodes SP_STATUS reads to zero and discards its writes. DPC_START and DPC_END writes are unsupported. The upstream source currently has the same fundamental assumptions. [RSPRecomp source](https://github.com/N64Recomp/N64Recomp/blob/main/RSPRecomp/src/rsp_recomp.cpp).

Thus stock RSPRecomp output cannot implement BOSS's signalling protocol. Either extend generation/runtime to expose real SP/DPC state and suspend/resume at waits, or integrate an RSP execution implementation with those facilities. Ignoring waits or reporting tasks complete immediately risks deadlocking the CPU or presenting stale data.

CPU recompilation additionally needs accurate function/section metadata and runtime overlay lookup; generated C alone does not provide the host OS or hardware bridge. The upstream README describes this architecture, but some feature-status text is stale relative to checked-out source, so inspect the actual configuration parser before assuming an option is unavailable. [N64Recomp README](https://github.com/N64Recomp/N64Recomp/blob/main/README.md).

## Concrete local reuse

Available sibling projects include `aerogauge-recomp`, `automobililamborghini-recomp`, and `Goemon64Recomp`, inspected in adjacent local checkouts.

- AeroGauge `src/main.cpp`: SDL startup, runtime callbacks, controller polling, bounded VI runs, useful boot summaries, and window/native-handle setup. Its ROM addresses and scene probes are game-specific and must not be copied as WDC facts.
- Both racing siblings' `src/rt64_renderer.cpp`: renderer context construction and VI presentation lifecycle. Adapt the RDP submission entry instead of their F3D task interpretation.
- Automobili Lamborghini's README and build structure provide a runnable Windows package/ROM-discovery pattern. Its release claim is a peer-project statement, not a WDC test result. [Peer repository](https://github.com/alondero/automobililamborghini-recomp).
- This repository's [peer conventions](../peer-projects.md) explicitly defer ROM symbols, RSP configuration, and renderer patches until derived from WDC.

## Recommended first-playable sequence

1. Generate and compile the CPU code with measured WDC function boundaries and OS symbols; establish progress through startup with explicit faults for unsupported hardware accesses.
2. Capture WDC SP task loading and register accesses. Confirm microcode identity and main/sublist handshake against the primary reference.
3. Execute BOSS RSP with accurate signalling and route produced RDP command ranges to RT64's existing raw RDP path. Keep audio in that same execution stream.
4. Connect VI presentation, input and audio output through sibling host patterns.
5. Verify an actual rendered title/menu, input response, and a moving race. A bootstrap executable, blank window, or non-crashing headless run does not satisfy the playable milestone.

The combined graphics/audio RSP is the main newly identified integration risk. No evidence gathered here establishes that an unchanged stock N64ModernRuntime plus RT64 can boot WDC.

## ROM-derived microcode identity and task layout

The supplied USA ROM is 16 MiB, SHA-256 `e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97`. Disassembly of CPU function `0x80024F08` (ROM `0x25B08`) directly constructs the following OSTask fields; addresses are not borrowed from a peer game:

| Field | RDRAM address/value | ROM offset | Length |
| --- | --- | --- | --- |
| type | 1 (graphics) | n/a | n/a |
| flags | 0 | n/a | n/a |
| ucode_boot | `0x8007C6B0` | `0x7D2B0` | `0xD0` |
| ucode | `0x8007C780` | `0x7D380` | `0xF60` |
| ucode_data | `0x800A1CD0` | `0xA28D0` | `0xF10` |

The task storage is double-buffered at `0x800B37B0 + index * 64`; the current index is held at `0x8007ED80` and toggles with XOR 1. Display-list start and byte length are stored at task offsets `0x30` and `0x34` from the builder's `a0` and `a1-a0`. Reproduce with `tools/rom/inspect_code.py 0x25B08 --size 0xC4` using Python with rabbitizer installed.

The boot instructions load main code into IMEM `0x1080`. Therefore the initial RSPRecomp inputs should use `text_offset = 0x7D380`, `text_size = 0xF60`, and `text_address = 0x1080`. Code ends at ROM `0x7E2E0`; code following the CPU range must not be classified as more CPU functions.

A direct instruction scan of that main RSP range finds COP0 reads of registers 4, 5, 6, 10, 11 and writes of 0, 1, 2, 3, 4, 8, 9, 11. These include SP_STATUS, SP DMA state, DPC_CURRENT, DPC_STATUS, and DPC_START/END. The final routine sets SP_STATUS with `0x12810` and polls signal 3 (`0x400`). This confirms that the graphics-specific COP0 integration is required by this actual ROM.

GLideN64's [microcode identification table](https://github.com/gonetz/GLideN64/blob/master/src/GBI.cpp) lists WDC USA CRC `0x553538CC` and European CRC `0x75ED44CC`. A naive CRC32 over the ROM's next 4096 bytes did not match; this is not used as validation of the ROM-derived location. The task builder and executable RSP instructions provide the positive evidence instead.

## CPU/RSP scheduling and verified hook sites

The following mappings come from disassembly of the same ROM, not symbol-name similarity. MMIO addresses and caller argument patterns establish each role.

| CPU entry | Role | Evidence |
| --- | --- | --- |
| `0x80077500` | `__osSpSetStatus(a0)` | Store a0 to `0xA4040010` |
| `0x800778A0` | `__osSpSetPc(a0)` | Require SP halt; store a0 to `0xA4080000` |
| `0x800778E0` | `__osSpRawStartDma` | Check busy, set SP/DRAM addresses and read/write length |
| `0x80077970` | `__osSpDeviceBusy` | Return whether SP_STATUS mask `0x1C` is nonzero |
| `0x8007175C` | `osSpTaskLoad` | Physicalize task, set status `0x2B00`, load task to DMEM FC0 and boot to IMEM 1000 |
| `0x800718EC` | `osSpTaskStartGo` | Wait for DMA idle; write status `0x125` |
| `0x80071620` | `osSpTaskYield` | Write status `0x400`, setting SIG0 |

For WDC, the apparent yield call is part of the main/sublist protocol. `0x80024094` finishes the CPU-built sublist and calls it at `0x80024160`. Cleanup `0x800252D0` writes BOSS EndSubDL (`0x1A000000`) at the list pointer stored in `0x8007ED50`, flushes caches, sets SIG0 at `0x80025320`, then waits for SP/DP completion. Hooking yield as a no-op breaks this protocol.

Normal submission calls TaskLoad/StartGo at `0x800251FC`/`0x80025204`. An auxiliary submission function `0x8002523C` constructs a main list containing EndMainDL (`0x02000000`) and submits it at `0x800252B0`/`0x800252B8`.

### SIG3 notification is acknowledged by the exception handler

At CPU `0x8007522C`, the SP interrupt handler reads SP_STATUS and retains mask `0x300` (SIG1/SIG2). It then unconditionally writes `0x8008` to SP_STATUS: clear SP interrupt and clear SIG3. If the retained mask was nonzero, it dispatches event table offset `0x20` (event 4, SP). Otherwise it dispatches offset `0x58` (event 11, SP_BREAK). This establishes that SIG3 acknowledgement does not depend on the application consuming its queue message.

For a native runtime, a suitable replacement applies the RSP's status command, classifies the pending SP interrupt using the resulting SIG1/SIG2, acknowledges IRQ and clears SIG3, then enqueues the registered event 4 or 11. Preserve memory visibility before sending a message. This is a proposed equivalent hook; it still requires execution testing. Ordinary completion must occur when the RSP breaks, not when the graphics task is initially enqueued.

### Event queues and producer/consumer functions

| Event/path | Registration or producer | Consumer |
| --- | --- | --- |
| SP_BREAK (11) | `0x80024178`: queue `0x800B2B40`, backing `0x800B2B58`, capacity 512, message `0x800AAAC8` | `0x80023F88`: nonblocking receive followed by blocking receive if needed; decrement pending count `0x8007ED74` |
| BOSS WaitSignal command | `0x80023F2C`: increment pending count; append `0x12000000,0` to list pointer `0x8007ED40` | RSP generates SIG3/SP interrupt after preceding vertex work |
| SP complete (4) | Event-forwarding thread `0x800017E0` registers queue `0x800AA930`, capacity 16, then forwards messages to `0x800AA918` | `0x800005F4` blocks on queue `0x800AA918` |
| DP complete (9) | Event-forwarding thread `0x80001888` registers queue `0x800AA9B8`, capacity 16, then forwards to `0x800AA988` | `0x80000678` blocks on queue `0x800AA988`; called by `0x800013DC` when graphics-active word `0x8007EDB8` is nonzero |

The small initialization helpers `0x800005A4` and `0x80000628` initially register SP and DP directly to their downstream queues; forwarding threads replace those registrations later. Use the runtime's event registration mechanism rather than hardcoding one queue address.

### Signal bit conventions

SP_STATUS read signal n is bit `7+n`. Write commands are separate: clear signal n is bit `9+2*n`, set is bit `10+2*n`. Thus SIG0 read=`0x80`, clear=`0x200`, set=`0x400`; SIG3 read=`0x400`, clear=`0x8000`, set=`0x10000`. Interrupt clear/set commands are `0x8`/`0x10`; paired clear+set writes must follow register semantics rather than treating a command as a status value. These conventions agree with the primary [mupen64plus RSP register implementation](https://github.com/mupen64plus/mupen64plus-core/blob/master/src/device/rcp/rsp/rsp_core.c).

### Startup and TLB scope

`0x800006E0` through `0x8000109C` initializes display configuration structures rooted at `0x800AAE38`, selects PAL/NTSC templates using word `0x80000300`, and sets up framebuffer allocation via `0x8003C9A4`/`0x8003C95C`. There is no direct MMIO or TLB instruction in this function.

The TLB instructions found in the complete measured CPU region are `tlbwi` at `0x800706F0` and `0x800770D8`, and `tlbp` at `0x8007B684`. `0x800706D0` clears TLB entries 30 down to 0 (osUnmapTLBAll pattern); `0x800770A0` sets the special entry 31 (osMapTLBRdb pattern). These do not establish game-specific TLB-mapped executable overlays. Replacing SDK initialization with native runtime support is preferable to inferring that game memory requires general TLB emulation from these routines alone.

## Callback coverage and shared assembly tails

A scan of aligned code pointers in ROM data `0x7E2E0..0xB0000`, plus LUI/ADDIU code-address constructions, found the following missing leaf callbacks in the initial symbol map. Each has a preceding completed return, a standalone body, and a pointer reference:

| Callback entry | Pointer evidence |
| --- | --- |
| `0x80002B30` | ROM data `0x7E880` |
| `0x80002CA0` | ROM data `0x7E8E8` |
| `0x80002D44` | ROM data `0x7E8B0` |
| `0x80002D64` | ROM data `0x7E8E0` |
| `0x80002D94` | ROM data `0x7E8A0` |
| `0x80002DB4` | ROM data `0x7E848` |
| `0x80002DD4` | ROM data `0x7E8D0` |
| `0x8003EF9C` | Constructed at CPU `0x8003EFF4`, passed as allocation callback |

These are suitable reviewed additions to `rom-symbols.toml` boundaries. Hundreds of other apparent code pointers are switch-table case labels. For example, `0x800601FC`, `0x8006023C`, `0x80060268`, `0x80060294`, `0x800602E0`, and `0x8006032C` are internal cases, and the apparent `0x80057D00` address lands in a preceding return's delay slot. Do not add every pointer as a function.

Four vector helpers require more than boundary additions. They save incoming RA in T9, construct a local continuation in RA, fall through into a shared squared-length body, and then return via T9 after square root:

| Public entry | Intended operation | Local continuation |
| --- | --- | --- |
| `0x80019FC0` | 3D vector length, input a0 | `0x80019FF0` |
| `0x8001A220` | 3D distance, inputs a0/a1 | `0x8001A268` |
| `0x8001A574` | 2D distance, inputs a0/a1 | `0x8001A5A8` |
| `0x8001A5C0` | 2D vector length, input a0 | `0x8001A5E8` |

The generated C inspected during bring-up had `func_80019FC0` and `func_8001A220` terminate immediately after constructing RA; their separately generated shared bodies also translated `jr ra` as an unconditional host return. This silently loses the length/distance calculation. Native replacements based on the measured instructions or an explicit generator correction must retain the entire computation and continuation. Adding continuation lookup entries alone is insufficient.

## Additional SDK identities

`0x8006F5E0` is **osViGetCurrentFramebuffer**, not NextFramebuffer. Both masked peer signatures match because their only difference is a global address. WDC's getter loads context pointer `0x80099EF0`, whereas its `osViSwapBuffer` writes next context `0x80099EF4`.

The following PFS identities were confirmed by ROM disassembly and the corresponding [libreultra initialization](https://github.com/n64decomp/libreultra/blob/master/src/io/pfsinitpak.c), [free-block](https://github.com/n64decomp/libreultra/blob/master/src/io/pfsfreeblocks.c), and [file-count](https://github.com/n64decomp/libreultra/blob/master/src/io/pfsnumfiles.c) implementations:

- `0x80071E10`: osPfsInitPak(queue, pfs, channel); obtains SI access, checks status, initializes pfs fields, selects bank zero and reads ID block 1.
- `0x80072560`: osPfsFreeBlocks; scans bank inode entries for free marker 3 and returns free pages times 256 through a1.
- `0x800726B0`: osPfsNumFiles; scans directory entries and writes used count through a2 and maximum through a1.
- Associated measured call targets: `0x800759D0` SI access acquisition, `0x80075A14` release, `0x800779A0` PFS status, `0x800788CC` bank select, `0x80078940` controller RAM read, `0x800785AC` inode read/write.

Neither racing sibling's masked osSpTaskYielded body matched this ROM. No speculative address is assigned to it.

### Precise math replacement contracts

All four length/distance helpers read packed 32-bit float components through guest pointers. They have no output pointer and do not write guest memory; the scalar result is F0. Distances subtract components of a0 from a1. Preserve individual single-precision subtraction, multiplication and addition ordering before `sqrtf` (3D sum is `(x*x + y*y) + z*z`).

- `0x80019FC0`: a0 points to three components; final F4=x, F6=y*y, F8=z*z, F0=length. T9 receives incoming RA; RA ends as `0x80019FF0`.
- `0x8001A220`: a0/a1 point to two three-component vectors; final F4=dx, F6=dy*dy, F8=dz*dz, F10=a1.x, F16=a1.y, F18=a1.z, F0=distance. T9 receives incoming RA; RA ends as `0x8001A268`.
- `0x8001A574`: a0/a1 point to two two-component vectors; final F4=dx, F6=dy*dy, F10=a1.x, F16=a1.y, F0=distance. T9 receives incoming RA; RA ends as `0x8001A5A8`. This real entry was not present in the initial symbol map and no direct call was found by that map's scan.
- `0x8001A5C0`: a0 points to two components; final F4=x, F6=y*y, F0=length. T9 receives incoming RA; RA ends as `0x8001A5E8`.

Independently callable shared entries `0x80019FCC`, `0x8001A22C`, and `0x8001A5CC` return squared length/distance without square root and do not set the continuation registers. They must retain that behavior.

A broader short-function inspection found additional initial symbol boundaries that split simple fallthrough wrappers: `0x80019E20` (8 bytes), `0x80019F90` (4), `0x8001A000` (8), `0x8001A040` (12), `0x8001A0C0` (8), `0x8001A100` (8), `0x8001A140` (8), `0x8001A1C0` (4), `0x8001A2AC` (4), `0x8001A360` (8), `0x8001A3E0` (8), `0x8001A440` (8), `0x8001A4C0` (4), and `0x8001A520` (4). Every listed fragment consists only of argument adjustment or constant setup and lacks a branch/return. They must continue into the next measured entry; either extend their function ranges across that shared body or emit explicit fallthrough calls. Keep the shared next entries independently callable. This is distinct from the computed-RA square-root continuations above.

### Pak detection and additional SI entrypoints

`0x800748F0` is `osPfsIsPlug(OSMesgQueue *queue, u8 *pattern)`: a0 is the queue, a1 points to a single output byte. It runs command-zero status queries with three retries, inspects each controller's accessory status, writes a bitmask and returns the SI transfer result. This exactly matches [libreultra pfsisplug.c](https://github.com/n64decomp/libreultra/blob/master/src/io/pfsisplug.c).

The inspected runtime `librecomp/src/pak.cpp` supplies no IsPlug entry; its other PFS operations report PFS_ERR_NOPACK. A consistent no-pak implementation of this synchronous detection function writes byte zero through guest a1 and returns zero, meaning detection succeeded with no accessory bits. It must not return NOPACK as if this were a per-pak file operation. No additional completion message is required when replacing the complete synchronous function. Controller device info should likewise advertise `Pak::None` so `osContInit` does not contradict detection.

Additional ROM-verified public SI routes:

- `0x80073730`: osMotorStop; CRC comparison uses buffer `0x800E24A0`, initialized to zero by MotorInit.
- `0x800738D0`: osMotorStart; CRC comparison uses buffer `0x800E24C0`, initialized to one by MotorInit. These conform to [libreultra motor.c](https://github.com/n64decomp/libreultra/blob/master/src/io/motor.c).
- `0x80073ED0`: osContStartQuery(queue); sends command-zero controller request, waits for the write and initiates the read before returning.
- `0x80073F54`: osContGetQuery(status); passes a stack bitpattern and the supplied status array to the same decode helper used by controller initialization.
- `0x80075A40`: underlying raw SI DMA routine reached by generated versions of these public functions.

Use native runtime wrappers for the public calls where available. Disabling rumble/controller-pak support does not make the game's accessory detection calls disappear.

## Runtime instruction rewriting in the geometry emitters

The crash at `0x80027414` after selecting Play Anyway was traced to translating placeholder instructions from ROM rather than their runtime replacements. The immediate chain at `0x800273DC..0x80027400` forces S0 to zero if executed literally.

ROM function `0x80037F20` is the actual code rewriter; caller `0x800380A4` invokes it on entries `0x80025410`, `0x8002703C`, and `0x80026208`. It scans each body until exact `jr ra` (`0x03E00008`). For an XORI opcode it uses the immediate's upper nibble as a count, retains that instruction's source-register field, and replaces that many subsequent instructions whose low twelve bits equal `0xBFD` with CACHE opcode 0x0D at offsets 0,16,32,... from the original source register.

The generic routine additionally searches the next six instructions for an OR-based move into the original source register. If found, it clears that move and rewrites store source-base fields in twelve words beginning immediately after the final sentinel. A faithful read-only simulation found no such move cases in the supplied ROM.

The resulting static oracle has exactly seventeen modified words:

| Target entry | Marker instruction addresses | Runtime replacement words |
| --- | --- | --- |
| `0x80025410` | none | none |
| `0x80026208` | `0x80026740`, `0x80026744`, `0x80026748`, `0x80026750` | `BD4D0000`, `BD4D0010`, `BD4D0020`, `BD4D0030` |
| `0x8002703C` | `0x800272D8`, `0x800272DC`, `0x800272E4`, `0x800272F8` | `BE0D0000`, `BE0D0010`, `BE0D0020`, `BE0D0030` |
| `0x8002703C` | `0x800273DC`, `0x800273E0`, `0x800273E4`, `0x800273EC`, `0x80027400` | `BE0D0000`, `BE0D0010`, `BE0D0020`, `BE0D0030`, `BE0D0040` |
| `0x8002703C` | `0x8002763C`, `0x80027640`, `0x80027648`, `0x8002765C` | `BE0D0000`, `BE0D0010`, `BE0D0020`, `BE0D0030` |

This is evidence of runtime cache-operation insertion, not evidence of anti-piracy. The correct native translation is to apply the measured transformation before recompiling these bodies, then lower CACHE to no-op under the existing coherent-host-memory contract. The original game rewriter may still execute against guest instruction bytes. Compare a runtime text snapshot to these seventeen replacements to validate the static simulation; no ROM file modification is required.

## Post-title translation and RDP review

ROM entry `0x80019F34` compares unsigned-byte NUL-terminated strings at a0 and a1 and returns exactly -1, 0, or +1. Its equal-byte loop branches backward to `0x80019F2C`, before the public entry, to test whether the previous matched byte was NUL. Simply ending/restarting a generated function at the public entry loses this loop. A native replacement must load both bytes, increment a0 on every comparison, increment a1 only when the bytes match (including the terminating NUL), and return the sign of the first difference. T0/T1 retain the final tested byte values; no memory is written. This contract is directly measured from ROM instructions `0x80019F2C..0x80019F58`.

The fourteen reviewed fallthrough wrappers listed above were checked again against the generated symbol ranges: all now contain their actual `jr ra` and its delay slot. This validates these measured cases, not arbitrary function inference. The generator's next-entry extension does not automatically inherit an explicit override on that next entry, and its forward-branch expansion does not cover backward shared tails such as the string comparison.

RT64 already buffers partial raw-RDP commands across calls. In `lib/rt64/src/hle/rt64_interpreter.cpp`, `processRDPLists` lines 78–106 finish a previously buffered command, and lines 129–136 retain the unfinished end of a batch. `Application::processDisplayLists(..., false)` invokes that synchronous LLE path without resetting its pending command state or raising an SP interrupt. The current BOSS sink passes buffers with that contract. Consequently, batch splitting alone is not evidence of the remaining narrow 2D presentation issue. Compare emitted SetColorImage width, SetScissor bounds, texture-rectangle coordinates, and scene-specific VI width/X scale before changing buffering. Primary source: [RT64 interpreter](https://github.com/rt64/rt64/blob/main/src/hle/rt64_interpreter.cpp) and [RT64 application](https://github.com/rt64/rt64/blob/main/src/hle/rt64_application.cpp), with the checked-out source providing the exact inspected revision.
