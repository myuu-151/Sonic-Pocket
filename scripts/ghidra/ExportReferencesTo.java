// Exports references to selected addresses as CSV.
// @category SonicPocket

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

public class ExportReferencesTo extends GhidraScript {
    private static String csv(String value) {
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Usage: ExportReferencesTo.java <output-file> <address> [address ...]"
            );
        }

        Path outputPath = Path.of(arguments[0]).toAbsolutePath();
        Path outputDirectory = outputPath.getParent();
        if (outputDirectory != null) {
            Files.createDirectories(outputDirectory);
        }

        StringBuilder output = new StringBuilder(
            "target,from,function,reference_type,code_unit\n"
        );
        ReferenceManager references = currentProgram.getReferenceManager();

        for (int index = 1; index < arguments.length; index++) {
            long offset = Long.parseUnsignedLong(
                arguments[index].replaceFirst("^0[xX]", ""), 16
            );
            Address target = toAddr(offset);
            ReferenceIterator iterator = references.getReferencesTo(target);
            int count = 0;

            while (iterator.hasNext()) {
                Reference reference = iterator.next();
                Address from = reference.getFromAddress();
                Function function = currentProgram.getFunctionManager().getFunctionContaining(from);
                CodeUnit codeUnit = currentProgram.getListing().getCodeUnitContaining(from);

                output.append(csv(target.toString())).append(',')
                    .append(csv(from.toString())).append(',')
                    .append(csv(function == null ? "" : function.getName())).append(',')
                    .append(csv(reference.getReferenceType().toString())).append(',')
                    .append(csv(codeUnit == null ? "" : codeUnit.toString())).append('\n');
                count++;
            }

            println("Found " + count + " references to " + target);
        }

        Files.writeString(outputPath, output.toString(), StandardCharsets.UTF_8);
        println("Wrote references to " + outputPath);
    }
}
