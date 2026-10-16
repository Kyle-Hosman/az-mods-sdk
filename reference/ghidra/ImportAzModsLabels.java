// Name the EP147 functions, vtables and data that the AZ-MODS reference documents.
//
// Open EP147 (from your own deck, or the emulator's pulled system) in Ghidra, then run
// this script from the Script Manager (or headless with -postScript). It reads
// reference/data/addresses.json, picks the firmware column by the program's MD5 (or
// asks), and for every entry:
//   function      creates the function if needed and names it
//   vtable        labels it and adds a "vtable for <class>" comment
//   rodata, data  labels it
//   mid-function  adds a comment only (it is not an entry point)
// Entries without a name get azmods_<kind>_<address>. Running it again changes nothing
// that is already in place.
//@category AZ-MODS

import java.io.File;
import java.io.FileReader;
import java.io.Reader;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ImportAzModsLabels extends GhidraScript {

    private File findDataFile() throws Exception {
        File script = getSourceFile() != null ? getSourceFile().getFile(false) : null;
        if (script != null) {
            File candidate = new File(script.getParentFile().getParentFile(), "data/addresses.json");
            if (candidate.isFile()) {
                return candidate;
            }
        }
        return askFile("AZ-MODS reference: addresses.json", "Use");
    }

    private String pickFirmware(JsonObject firmware) throws Exception {
        String md5 = currentProgram.getExecutableMD5();
        md5 = md5 == null ? "" : md5.toLowerCase();
        List<String> keys = new ArrayList<>();
        for (Map.Entry<String, JsonElement> e : firmware.entrySet()) {
            keys.add(e.getKey());
            JsonElement m = e.getValue().getAsJsonObject().get("md5");
            if (m != null && m.getAsString().equalsIgnoreCase(md5)) {
                return e.getKey();
            }
        }
        if (isRunningHeadless()) {
            throw new IllegalStateException("this program's MD5 (" + md5 + ") matches no documented EP147 build");
        }
        return askChoice("EP147 build", "This program's MD5 (" + md5 + ") matches no documented build. Which column?",
            keys, keys.get(keys.size() - 1));
    }

    private static String str(JsonObject o, String key) {
        JsonElement e = o.get(key);
        return e == null || e.isJsonNull() ? "" : e.getAsString();
    }

    private boolean setLabel(Address addr, String name) throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        for (Symbol s : st.getSymbols(addr)) {
            if (s.getName().equals(name)) {
                return false;
            }
        }
        st.createLabel(addr, name, SourceType.USER_DEFINED).setPrimary();
        return true;
    }

    private void setComment(Address addr, int type, String text) {
        Listing listing = currentProgram.getListing();
        String old = listing.getComment(type, addr);
        if (!text.equals(old)) {
            listing.setComment(addr, type, text);
        }
    }

    @Override
    protected void run() throws Exception {
        JsonObject data;
        try (Reader r = new FileReader(findDataFile())) {
            data = JsonParser.parseReader(r).getAsJsonObject();
        }
        JsonObject meta = data.getAsJsonObject("meta");
        String column = pickFirmware(meta.getAsJsonObject("firmware"));
        int named = 0, created = 0, comments = 0, skipped = 0;
        for (JsonElement el : data.getAsJsonArray("addresses")) {
            JsonObject e = el.getAsJsonObject();
            String text = str(e, column);
            if (text.isEmpty()) {
                skipped++;
                continue;
            }
            Address addr = toAddr(Long.decode(text));
            if (currentProgram.getMemory().getBlock(addr) == null) {
                skipped++;
                continue;
            }
            String kind = str(e, "kind");
            String cls = str(e, "class");
            String note = str(e, "meaning");
            String name = str(e, "name");
            if (name.isEmpty()) {
                name = kind.equals("vtable") && !cls.isEmpty() ? "vtable_" + cls.replace("::", "_")
                    : "azmods_" + kind.replace('-', '_') + "_" + text.substring(2);
            }
            if (kind.equals("mid-function")) {
                setComment(addr, CodeUnit.EOL_COMMENT, ("AZ-MODS: " + name + " " + note).trim());
                comments++;
                continue;
            }
            if (kind.equals("function")) {
                Function f = getFunctionAt(addr);
                if (f == null) {
                    disassemble(addr);
                    f = createFunction(addr, name);
                    if (f != null) {
                        created++;
                    }
                }
                if (f != null && !f.getName().equals(name)) {
                    f.setName(name, SourceType.USER_DEFINED);
                    named++;
                }
                else if (f == null && setLabel(addr, name)) {
                    named++;
                }
            }
            else if (setLabel(addr, name)) {
                named++;
            }
            if (kind.equals("vtable") && !cls.isEmpty()) {
                setComment(addr, CodeUnit.PLATE_COMMENT, "vtable for " + cls);
            }
            if (!note.isEmpty()) {
                setComment(addr, CodeUnit.EOL_COMMENT, note);
            }
        }
        println(String.format("AZ-MODS labels (%s, %s): %d named, %d functions created, %d mid-function comments, %d skipped",
            column, str(meta, "source"), named, created, comments, skipped));
    }
}
