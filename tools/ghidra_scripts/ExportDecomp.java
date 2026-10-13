// Export Ghidra's decompilation of every function, plus a function index.
// usage (headless): -postScript ExportDecomp.java <output dir>
// @category Arena

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;

public class ExportDecomp extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : "decomp_out");
		outDir.mkdirs();

		DecompInterface ifc = new DecompInterface();
		ifc.setOptions(new DecompileOptions());
		ifc.openProgram(currentProgram);

		int total = 0, failed = 0;
		try (PrintWriter src = new PrintWriter(new File(outDir, "all_functions.c"), "UTF-8");
				PrintWriter idx = new PrintWriter(new File(outDir, "functions.csv"), "UTF-8");
				PrintWriter seg = new PrintWriter(new File(outDir, "segments.csv"), "UTF-8")) {

			seg.println("name,start,end,size,execute");
			for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
				seg.printf("%s,%s,%s,%d,%b%n", b.getName(), b.getStart(), b.getEnd(), b.getSize(),
					b.isExecute());
			}

			idx.println("address,name,size,callers,callees,decompiled");
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				total++;
				DecompileResults res = ifc.decompileFunction(f, 60, monitor);
				boolean ok = res != null && res.decompileCompleted();
				src.printf("/* ---- %s @ %s (%d bytes) ---- */%n", f.getName(), f.getEntryPoint(),
					f.getBody().getNumAddresses());
				if (ok) {
					src.println(res.getDecompiledFunction().getC());
				}
				else {
					failed++;
					src.printf("/* decompilation failed: %s */%n%n",
						res == null ? "null" : res.getErrorMessage());
				}
				idx.printf("%s,%s,%d,%d,%d,%b%n", f.getEntryPoint(), f.getName(),
					f.getBody().getNumAddresses(), f.getCallingFunctions(monitor).size(),
					f.getCalledFunctions(monitor).size(), ok);
			}
		}
		ifc.dispose();
		println(String.format("ExportDecomp: %d functions, %d failed -> %s", total, failed,
			outDir.getAbsolutePath()));
	}
}
