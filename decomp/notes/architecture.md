# How ACD.EXE is built

Findings from `features.csv`, `calls.csv` and `classes.csv` (all in
`decomp/raw/`), plus hand-read samples in `sample.asm` / `sample2.asm`.

## Mostly hand-written 386 assembly

`classes.csv` labels each function `C` or `asm` from how it is called and
built (stack frame, stack arguments, caller stack cleanup):

| | Bytes | Share |
|---|---|---|
| Assembly | ~181,600 | ~91% |
| Compiled C | ~18,400 | ~9% |

The heuristic is rough; individual labels can be wrong, the totals are not
far off.

The assembly:
- passes arguments in registers (AX, BX, CX, DX, SI, DI, ES:DI, DS:SI) and
  often reports errors in the carry flag
- saves/restores registers itself, no BP stack frames
- uses 386 instructions throughout: 32-bit registers (EAX...), FS, MOVZX,
  PUSHA/POPA, REP MOVSD, PUSH imm. The game requires a 386.
- talks to hardware directly: VGA memory at A000, EMS via INT 67h, CMOS/RTC
  ports 70h/71h, mouse INT 33h, video BIOS INT 10h.
- reloads DS = DGROUP (0x4BA0) itself where needed.

Ghidra's C output for these functions is not trustworthy: it loses segment
registers (a 64,000-byte copy to A000:0000 shows up as a write to DS:0),
misreads register arguments, and treats constants as pointers. Translate
assembly to C from the disassembly (`DumpAsm.java`), not from
`all_functions.c`.

Ghidra listing quirk: `MOV Sreg,reg` (opcode 8E) is displayed with operands
swapped, e.g. `8ed8` prints as `MOV AX,DS` but is `MOV DS,AX`. Read the
opcode bytes when it matters.

## Compiled C (Borland)

Mostly in segment `1ffd` and the last three code segments `3f9f`, `4065`,
`4159`, with a few dozen functions elsewhere. Borland cdecl: arguments pushed
right to left, far calls, caller cleans up with `ADD SP,n` (or `POP CX`).

## C-to-asm wrappers

Many small "C" functions are thin bridges from C into the assembly:
`push bp; mov bp,sp`, load stack arguments into registers, `call` the asm
routine, `pop bp; retf`. Examples: `1000:6c26`, `1000:51d3`, `1000:8d14`
(85 callers), `1000:5e3e`. When translating, these become the C prototypes
of the asm routines they wrap; the wrapper itself disappears.

## Other notable pieces (hypotheses, to confirm)

- `1ffd:0d4c`, `1ffd:0de8`, ... : 6-byte stubs `mov ax,<n>; jmp 2194:0544`,
  a numbered-function dispatcher. Possibly the sound-driver (.ADV) API.
- `1ffd:1002`, `1ffd:10b5`, ...: INT 66h with AH=06. Purpose unknown yet
  (driver interface?).
- `1ffd:03c4` / `1ffd:03df`: read/write CMOS via ports 70h/71h (real-time clock).
- `4065:0455` area: bit-level reader, likely a decompressor for data files.
- `1000:5c8a`, `1000:6c36`, `1000:9cd6`: custom EMS-backed memory manager
  (INT 67h AX=4E02 swaps mapping contexts).
- `1000:946a`: copies a 320x200 frame (0x3E80 dwords) to VGA memory.

## Borland runtime library

Almost none is linked. Besides the `c0` startup at `1000:0000`, the game
appears to use its own assembly routines for memory, files and strings.
Still to check: whether any small Borland helpers (long multiply/divide,
`N_LXMUL@`-style) are present.
