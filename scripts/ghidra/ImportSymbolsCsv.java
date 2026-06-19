// Imports the project's human-authored symbol database into the current program.
// @category SonicPocket

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ImportSymbolsCsv extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException("Usage: ImportSymbolsCsv.java <symbols.csv>");
        }

        Path csvPath = Path.of(arguments[0]);
        List<String> lines = Files.readAllLines(csvPath, StandardCharsets.UTF_8);
        SymbolTable symbolTable = currentProgram.getSymbolTable();
        int applied = 0;

        for (int index = 1; index < lines.size(); index++) {
            String line = lines.get(index).trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }

            String[] fields = line.split(",", 4);
            if (fields.length < 3) {
                printerr("Skipping malformed CSV row " + (index + 1) + ": " + line);
                continue;
            }

            long offset = Long.parseUnsignedLong(fields[0].trim().replaceFirst("^0[xX]", ""), 16);
            String name = fields[1].trim();
            String confidence = fields[2].trim();
            Address address = toAddr(offset);
            Function function = currentProgram.getFunctionManager().getFunctionAt(address);

            if (function != null) {
                if (!function.getName().equals(name)) {
                    function.setName(name, SourceType.USER_DEFINED);
                }
                println("Function " + address + " -> " + name + " [" + confidence + "]");
            }
            else {
                Symbol primary = symbolTable.getPrimarySymbol(address);
                if (primary == null) {
                    symbolTable.createLabel(address, name, SourceType.USER_DEFINED);
                }
                else if (!primary.getName().equals(name)) {
                    primary.setName(name, SourceType.USER_DEFINED);
                }
                println("Label    " + address + " -> " + name + " [" + confidence + "]");
            }
            applied++;
        }

        println("Applied " + applied + " symbols from " + csvPath);
    }
}
