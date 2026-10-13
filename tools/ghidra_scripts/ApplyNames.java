// Rename functions from a CSV (address,name). Existing user-defined names
// are overwritten so re-running after editing the CSV is safe.
// usage (headless): -process <prog> -noanalysis -postScript ApplyNames.java <names.csv>
// @category Arena

import java.io.File;
import java.nio.file.Files;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyNames extends GhidraScript {

	@Override
	protected void run() throws Exception {
		List<String> lines = Files.readAllLines(new File(getScriptArgs()[0]).toPath());
		int done = 0, missing = 0;
		for (String line : lines.subList(1, lines.size())) {
			String[] p = line.trim().split(",");
			if (p.length < 2) {
				continue;
			}
			Function f = getFunctionAt(toAddr(p[0]));
			if (f == null) {
				missing++;
				continue;
			}
			f.setName(p[1], SourceType.USER_DEFINED);
			done++;
		}
		println(String.format("ApplyNames: %d renamed, %d addresses without a function", done,
			missing));
	}
}
