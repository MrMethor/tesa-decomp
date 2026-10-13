// Dump per-function features used to classify functions (runtime library vs.
// game vs. hand-written asm). Writes <out dir>/features.csv.
// Columns: address, size, insns, has_frame, far_ret, ints, ports, callers,
//          callees, dgroup_refs (sorted DGROUP offsets touched)
// "ints" lists interrupts as NN or NN:AH when AH is set by a constant just before.
// usage (headless): -process <prog> -noanalysis -readOnly -postScript FeatureDump.java <out dir>
// @category Arena

import java.io.File;
import java.io.PrintWriter;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.SegmentedAddress;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

public class FeatureDump extends GhidraScript {

	static final int DGROUP = 0x4BA0;

	@Override
	protected void run() throws Exception {
		File out = new File(getScriptArgs()[0], "features.csv");
		try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
			w.println("address,size,insns,has_frame,far_ret,ints,ports,callers,callees,dgroup_refs");
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				if (monitor.isCancelled()) {
					break;
				}
				int insns = 0, ports = 0;
				boolean farRet = false, frame = false;
				Integer lastAh = null;
				TreeSet<String> ints = new TreeSet<>();
				TreeSet<Integer> dg = new TreeSet<>();
				InstructionIterator it =
					currentProgram.getListing().getInstructions(f.getBody(), true);
				while (it.hasNext()) {
					Instruction ins = it.next();
					String m = ins.getMnemonicString().toUpperCase();
					String txt = ins.toString().toUpperCase();
					if (insns == 1 && txt.startsWith("MOV BP,SP")) {
						frame = true;
					}
					insns++;
					if (m.equals("RETF")) {
						farRet = true;
					}
					if (m.equals("IN") || m.equals("OUT") || m.startsWith("INS") ||
						m.startsWith("OUTS")) {
						ports++;
					}
					if (m.equals("INT")) {
						Scalar s = ins.getScalar(0);
						String n = s == null ? "?" : String.format("%02X", s.getUnsignedValue());
						ints.add(lastAh == null ? n : String.format("%s:%02X", n, lastAh));
					}
					// track "MOV AH,imm" / "MOV AX,imm" immediately feeding an INT
					if (m.equals("MOV") && (txt.startsWith("MOV AH,") || txt.startsWith("MOV AX,"))) {
						Scalar s = ins.getScalar(1);
						if (s != null) {
							long v = s.getUnsignedValue();
							lastAh = (int) (txt.startsWith("MOV AH,") ? v & 0xFF : (v >> 8) & 0xFF);
						}
						else {
							lastAh = null;
						}
					}
					else if (!m.equals("INT") && writesAx(txt)) {
						lastAh = null;
					}
					for (Reference r : ins.getReferencesFrom()) {
						Address to = r.getToAddress();
						if (to instanceof SegmentedAddress && r.getReferenceType().isData()) {
							SegmentedAddress sa = (SegmentedAddress) to;
							int lin = sa.getSegment() * 16 + sa.getSegmentOffset();
							int off = lin - DGROUP * 16;
							if (off >= 0 && off < 0x10000) {
								dg.add(off);
							}
						}
					}
				}
				StringBuilder dgs = new StringBuilder();
				for (int o : dg) {
					if (dgs.length() > 0) {
						dgs.append(';');
					}
					dgs.append(String.format("%04X", o));
				}
				w.printf("%s,%d,%d,%b,%b,%s,%d,%d,%d,%s%n", f.getEntryPoint(),
					f.getBody().getNumAddresses(), insns, frame, farRet, String.join(";", ints),
					ports, f.getCallingFunctions(monitor).size(),
					f.getCalledFunctions(monitor).size(), dgs);
			}
		}
		println("FeatureDump: wrote " + out.getAbsolutePath());
	}

	// Rough check whether an instruction overwrites AH (so a remembered AH is stale).
	private static boolean writesAx(String txt) {
		return txt.matches("^(MOV|POP|XCHG|LODS\\w*|IN|ADD|SUB|XOR|OR|AND|INC|DEC|SHL|SHR|MUL|DIV|IMUL|IDIV|CWD|CBW|LAHF|CALL\\w*)\\b.*") &&
			(txt.matches("^\\w+ (AX|AH)\\b.*") || txt.startsWith("CALL") || txt.startsWith("LODS") ||
				txt.startsWith("MUL") || txt.startsWith("DIV") || txt.startsWith("IMUL") ||
				txt.startsWith("IDIV") || txt.startsWith("CBW") || txt.startsWith("LAHF") ||
				txt.startsWith("POP AX") || txt.startsWith("IN "));
	}
}
