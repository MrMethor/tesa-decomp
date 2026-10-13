// Pre-analysis setup for ACD_unpacked.EXE (loaded at segment 0x1000).
//  - marks the data segments (0x4276 and up) non-executable
//  - sets DS = DGROUP (0x4BA0) for all code, as Borland's startup does
//  - disassembles and creates a function at every far-call target
// usage (headless): -preScript ArenaSetup.java <far_targets.txt>
// @category Arena

import java.io.File;
import java.math.BigInteger;
import java.nio.file.Files;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.SegmentedAddress;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class ArenaSetup extends GhidraScript {

	static final int FIRST_DATA_SEG = 0x4276;
	static final int DGROUP = 0x4BA0;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		Register ds = currentProgram.getRegister("DS");

		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			if (!(b.getStart() instanceof SegmentedAddress)) {
				continue;
			}
			int seg = ((SegmentedAddress) b.getStart()).getSegment();
			if (seg >= FIRST_DATA_SEG) {
				b.setExecute(false);
				b.setName(seg == DGROUP ? "DGROUP" : String.format("DATA_%04X", seg));
			}
			else {
				currentProgram.getProgramContext().setValue(ds, b.getStart(), b.getEnd(),
					BigInteger.valueOf(DGROUP));
			}
		}

		int created = 0, existing = 0, bad = 0;
		for (String line : Files.readAllLines(new File(args[0]).toPath())) {
			line = line.trim();
			if (line.isEmpty()) {
				continue;
			}
			int seg = Integer.parseInt(line.substring(0, 4), 16);
			if (seg >= FIRST_DATA_SEG) {
				continue;      // stray 0x9A byte inside data, not a real call
			}
			Address a = toAddr(line);
			if (a == null || currentProgram.getMemory().getBlock(a) == null) {
				bad++;
				continue;
			}
			new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
			Function f = getFunctionAt(a);
			if (f != null) {
				existing++;
			}
			else if (createFunction(a, null) != null) {
				created++;
			}
			else {
				bad++;
			}
		}
		println(String.format("ArenaSetup: %d functions created, %d existing, %d failed", created,
			existing, bad));
	}
}
