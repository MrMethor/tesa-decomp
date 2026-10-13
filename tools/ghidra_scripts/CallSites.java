// Dump every call site: caller, callee, near/far, and how the stack is
// cleaned up after it. Borland C callers either "ADD SP,n" or "POP CX"
// (2 bytes) / "POP CX; POP CX" (4 bytes) after calling a function with stack
// arguments; hand-written asm callers usually pass arguments in registers.
// Writes <out dir>/calls.csv: site, caller, callee, kind, cleanup, pushes_before
// usage (headless): -process <prog> -noanalysis -readOnly -postScript CallSites.java <out dir>
// @category Arena

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

public class CallSites extends GhidraScript {

	@Override
	protected void run() throws Exception {
		File out = new File(getScriptArgs()[0], "calls.csv");
		int n = 0;
		try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
			w.println("site,caller,callee,kind,cleanup,pushes_before");
			InstructionIterator it = currentProgram.getListing().getInstructions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Instruction ins = it.next();
				String m = ins.getMnemonicString().toUpperCase();
				if (!m.equals("CALL") && !m.equals("CALLF")) {
					continue;
				}
				Address target = null;
				for (Reference r : ins.getReferencesFrom()) {
					if (r.getReferenceType().isCall()) {
						target = r.getToAddress();
					}
				}
				Function caller = getFunctionContaining(ins.getAddress());
				Function callee = target == null ? null : getFunctionAt(target);
				String kind = m.equals("CALLF") ? "far" : "near";
				// "PUSH CS; CALL near" is a far call to a function in the same segment
				Instruction prev = ins.getPrevious();
				if (kind.equals("near") && prev != null &&
					prev.toString().toUpperCase().equals("PUSH CS")) {
					kind = "far-local";
				}
				w.printf("%s,%s,%s,%s,%d,%d%n", ins.getAddress(),
					caller == null ? "" : caller.getEntryPoint(),
					callee == null ? (target == null ? "indirect" : target.toString())
							: callee.getEntryPoint().toString(),
					kind, cleanup(ins.getNext()), pushesBefore(prev));
				n++;
			}
		}
		println("CallSites: " + n + " calls -> " + out.getAbsolutePath());
	}

	// Bytes of stack argument cleanup right after a call (0 if none).
	private int cleanup(Instruction next) {
		if (next == null) {
			return 0;
		}
		String t = next.toString().toUpperCase();
		if (t.startsWith("ADD SP,")) {
			// "ADD SP,reg" has no constant; report it as -1 (unknown size)
			return next.getScalar(1) == null ? -1 : (int) next.getScalar(1).getValue();
		}
		if (t.equals("POP CX")) {
			Instruction n2 = next.getNext();
			return n2 != null && n2.toString().toUpperCase().equals("POP CX") ? 4 : 2;
		}
		return 0;
	}

	// Count consecutive PUSH instructions directly before the call.
	private int pushesBefore(Instruction prev) {
		int c = 0;
		while (prev != null && prev.getMnemonicString().toUpperCase().equals("PUSH") && c < 16) {
			if (prev.toString().toUpperCase().equals("PUSH CS")) {
				prev = prev.getPrevious();
				continue;
			}
			c++;
			prev = prev.getPrevious();
		}
		return c;
	}
}
