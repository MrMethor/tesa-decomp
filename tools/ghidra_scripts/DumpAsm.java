// Write the disassembly of selected functions to a text file.
// usage (headless): -process <prog> -noanalysis -readOnly -postScript DumpAsm.java <out.txt> <addr> [<addr> ...]
// An address may also be "@file" naming a file with one address per line.
// @category Arena

import java.io.File;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class DumpAsm extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		List<String> addrs = new ArrayList<>();
		for (int i = 1; i < args.length; i++) {
			if (args[i].startsWith("@")) {
				for (String l : Files.readAllLines(new File(args[i].substring(1)).toPath())) {
					if (!l.isBlank()) {
						addrs.add(l.trim());
					}
				}
			}
			else {
				addrs.add(args[i]);
			}
		}
		try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
			for (String s : addrs) {
				Address a = toAddr(s);
				Function f = a == null ? null : getFunctionContaining(a);
				if (f == null) {
					w.printf("; no function at %s%n%n", s);
					continue;
				}
				w.printf("; ==== %s @ %s (%d bytes)%n", f.getName(), f.getEntryPoint(),
					f.getBody().getNumAddresses());
				InstructionIterator it =
					currentProgram.getListing().getInstructions(f.getBody(), true);
				while (it.hasNext()) {
					Instruction ins = it.next();
					StringBuilder hex = new StringBuilder();
					for (byte b : ins.getBytes()) {
						hex.append(String.format("%02x", b));
					}
					w.printf("%s  %-14s %s%n", ins.getAddress(), hex, ins);
				}
				w.println();
			}
		}
		println("DumpAsm: " + addrs.size() + " functions -> " + args[0]);
	}
}
