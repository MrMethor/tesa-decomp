// Disassemble undefined byte ranges in executable blocks and turn clean
// disassemblies into functions. Repeats until no new code is found.
// A range is only accepted if disassembly from its start produces no
// error bookmarks; otherwise the instructions are cleared again.
// usage (headless, existing project): -process <prog> -postScript FillGaps.java
// @category Arena

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.BookmarkType;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;

public class FillGaps extends GhidraScript {

	static final int MIN_GAP = 4;

	@Override
	protected void run() throws Exception {
		AddressSet exec = new AddressSet();
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			if (b.isExecute()) {
				exec.add(b.getStart(), b.getEnd());
			}
		}

		int pass = 0, made = 0, rejected = 0;
		boolean changed = true;
		while (changed && !monitor.isCancelled()) {
			changed = false;
			pass++;
			AddressSetView undef =
				currentProgram.getListing().getUndefinedRanges(exec, false, monitor);
			for (AddressRange r : undef) {
				if (r.getLength() < MIN_GAP) {
					continue;
				}
				Address start = skipPadding(r);
				if (start == null || currentProgram.getListing().getInstructionAt(start) != null) {
					continue;
				}
				int errorsBefore = errorCount();
				DisassembleCommand cmd = new DisassembleCommand(start, null, true);
				cmd.applyTo(currentProgram, monitor);
				AddressSetView got = cmd.getDisassembledAddressSet();
				Instruction first = currentProgram.getListing().getInstructionAt(start);
				if (first == null || got == null || got.isEmpty() || errorCount() > errorsBefore) {
					if (got != null && !got.isEmpty()) {
						clearListing(got);
					}
					rejected++;
					continue;
				}
				if (getFunctionContaining(start) == null && createFunction(start, null) != null) {
					made++;
				}
				changed = true;
			}
			analyzeChanges(currentProgram);
			println(String.format("FillGaps pass %d: %d functions made, %d ranges rejected so far",
				pass, made, rejected));
			if (pass >= 10) {
				break;
			}
		}
	}

	// Skip alignment/padding bytes (00 / 90 NOP) at the start of a gap.
	private Address skipPadding(AddressRange r) throws Exception {
		Address a = r.getMinAddress();
		while (a.compareTo(r.getMaxAddress()) <= 0) {
			byte b = getByte(a);
			if (b != 0x00 && b != (byte) 0x90) {
				return a;
			}
			a = a.next();
		}
		return null;
	}

	private int errorCount() {
		return currentProgram.getBookmarkManager().getBookmarkCount(BookmarkType.ERROR);
	}
}
