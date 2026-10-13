// For every function, list the 16-bit constants it uses: immediates,
// memory displacements and resolved data references (as offsets). Asm code
// loads string/table addresses as plain immediates (MOV SI,0x4b1b), which
// Ghidra does not turn into references, so this is the raw material for
// matching functions to DGROUP strings and globals.
// Writes <out dir>/operands.csv: address, values (hex, ';'-separated, unique)
// usage (headless): -process <prog> -noanalysis -readOnly -postScript OperandDump.java <out dir>
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

public class OperandDump extends GhidraScript {

	@Override
	protected void run() throws Exception {
		File out = new File(getScriptArgs()[0], "operands.csv");
		try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
			w.println("address,values");
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				TreeSet<Integer> vals = new TreeSet<>();
				InstructionIterator it =
					currentProgram.getListing().getInstructions(f.getBody(), true);
				while (it.hasNext()) {
					Instruction ins = it.next();
					for (int i = 0; i < ins.getNumOperands(); i++) {
						for (Object o : ins.getOpObjects(i)) {
							if (o instanceof Scalar) {
								long v = ((Scalar) o).getUnsignedValue() & 0xFFFF;
								if (v >= 0x10) {
									vals.add((int) v);
								}
							}
						}
					}
					for (Reference r : ins.getReferencesFrom()) {
						Address to = r.getToAddress();
						if (r.getReferenceType().isData() && to instanceof SegmentedAddress) {
							vals.add(((SegmentedAddress) to).getSegmentOffset() & 0xFFFF);
						}
					}
				}
				StringBuilder sb = new StringBuilder();
				for (int v : vals) {
					if (sb.length() > 0) {
						sb.append(';');
					}
					sb.append(String.format("%04X", v));
				}
				w.printf("%s,%s%n", f.getEntryPoint(), sb);
			}
		}
		println("OperandDump: wrote " + out.getAbsolutePath());
	}
}
