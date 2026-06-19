// Exports Ghidra listing code units for selected inclusive address ranges.
// @category SonicPocket

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CodeUnitIterator;
import ghidra.program.model.listing.Function;

public class ExportListingRanges extends GhidraScript {
    private static long parseOffset(String value) {
        return Long.parseUnsignedLong(value.replaceFirst("^0[xX]", ""), 16);
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 3 || arguments.length % 2 == 0) {
            throw new IllegalArgumentException(
                "Usage: ExportListingRanges.java <output-file> <start> <end> [start end ...]"
            );
        }

        Path outputPath = Path.of(arguments[0]).toAbsolutePath();
        Path outputDirectory = outputPath.getParent();
        if (outputDirectory != null) {
            Files.createDirectories(outputDirectory);
        }

        StringBuilder output = new StringBuilder();
        for (int index = 1; index < arguments.length; index += 2) {
            Address start = toAddr(parseOffset(arguments[index]));
            Address end = toAddr(parseOffset(arguments[index + 1]));
            output.append("# ").append(start).append('-').append(end).append('\n');

            CodeUnitIterator units = currentProgram.getListing().getCodeUnits(
                new AddressSet(start, end), true
            );
            int count = 0;
            while (units.hasNext()) {
                CodeUnit unit = units.next();
                Function function = currentProgram.getFunctionManager()
                    .getFunctionContaining(unit.getAddress());
                output.append(unit.getAddress()).append("  ")
                    .append(function == null ? "" : function.getName()).append("  ")
                    .append(unit).append('\n');
                count++;
            }
            if (count == 0) {
                output.append("(no defined code units)\n");
            }
            output.append('\n');
        }

        Files.writeString(outputPath, output.toString(), StandardCharsets.UTF_8);
        println("Wrote listing ranges to " + outputPath);
    }
}
