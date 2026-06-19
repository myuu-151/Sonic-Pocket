// Exports selected functions from the current program as decompiled C text.
// @category SonicPocket

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class ExportDecompilation extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Usage: ExportDecompilation.java <output-file> <address> [address ...]"
            );
        }

        Path outputPath = Path.of(arguments[0]).toAbsolutePath();
        Path outputDirectory = outputPath.getParent();
        if (outputDirectory != null) {
            Files.createDirectories(outputDirectory);
        }
        StringBuilder output = new StringBuilder();
        DecompInterface decompiler = new DecompInterface();

        try {
            if (!decompiler.openProgram(currentProgram)) {
                throw new IllegalStateException("Could not open the current program in the decompiler");
            }

            for (int index = 1; index < arguments.length; index++) {
                long offset = Long.parseUnsignedLong(
                    arguments[index].replaceFirst("^0[xX]", ""), 16
                );
                Address address = toAddr(offset);
                Function function = currentProgram.getFunctionManager().getFunctionAt(address);
                if (function == null) {
                    function = currentProgram.getFunctionManager().getFunctionContaining(address);
                }

                output.append("/* ").append(address).append(" */\n");
                if (function == null) {
                    output.append("/* No function defined at this address. */\n\n");
                    continue;
                }

                DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
                if (!results.decompileCompleted() || results.getDecompiledFunction() == null) {
                    output.append("/* Decompilation failed: ")
                        .append(results.getErrorMessage())
                        .append(" */\n\n");
                    continue;
                }

                output.append(results.getDecompiledFunction().getC()).append("\n\n");
            }
        }
        finally {
            decompiler.dispose();
        }

        Files.writeString(outputPath, output.toString(), StandardCharsets.UTF_8);
        println("Wrote decompilation to " + outputPath);
    }
}
