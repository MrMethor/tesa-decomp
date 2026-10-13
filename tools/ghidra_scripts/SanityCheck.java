// Flag functions that are probably data mis-disassembled as code.
// Writes <out dir>/suspicious.csv: address, size, reason.
// usage (headless): -process <prog> -noanalysis -postScript SanityCheck.java <out dir>
// @category Arena

import java.io.File;
import java.io.PrintWriter;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class SanityCheck extends GhidraScript {

	// Instructions that ordinary 1990s DOS game code (C or hand asm) won't use.
	static final Set<String> ODD = Set.of("HLT", "INTO", "BOUND", "ARPL", "LOCK", "INT3",
		"ESC", "WAIT", "AAA", "AAS", "AAM", "AAD", "DAA", "DAS", "SALC", "LAHF");

	@Override
	protected void run() throws Exception {
		File out = new File(getScriptArgs()[0], "suspicious.csv");
		int flagged = 0, total = 0;
		try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
			w.println("address,size,reason");
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				total++;
				int odd = 0, count = 0;
				boolean returns = false;
				InstructionIterator it =
					currentProgram.getListing().getInstructions(f.getBody(), true);
				while (it.hasNext()) {
					Instruction ins = it.next();
					count++;
					String m = ins.getMnemonicString().toUpperCase();
					if (ODD.contains(m) || m.startsWith("F")) {
						odd++;
					}
					if (m.startsWith("RET") || m.equals("IRET") || m.startsWith("JMP")) {
						returns = true;
					}
				}
				String reason = null;
				if (count > 0 && odd * 10 >= count && odd >= 2) {
					reason = "many unusual instructions (" + odd + "/" + count + ")";
				}
				else if (!returns && !f.hasNoReturn()) {
					reason = "no return or jump";
				}
				if (reason != null) {
					flagged++;
					w.printf("%s,%d,%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(),
						reason);
				}
			}
		}
		println(String.format("SanityCheck: %d of %d functions flagged -> %s", flagged, total,
			out.getAbsolutePath()));
	}
}
