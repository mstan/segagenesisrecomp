// Offline headless project preparation; no GUI, MCP server, or listening port.
// Compile/run with the installed Ghidra classpath (see DISASSEMBLIES.md).
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HexFormat;
import java.security.MessageDigest;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import ghidra.GhidraApplicationLayout;
import ghidra.GhidraLaunchable;
import ghidra.base.project.GhidraProject;
import ghidra.framework.Application;
import ghidra.framework.HeadlessGhidraApplicationConfiguration;
import ghidra.program.flatapi.FlatProgramAPI;
import ghidra.program.model.lang.LanguageID;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.util.DefaultLanguageService;
import ghidra.util.task.TaskMonitor;

public class SonicAnnotationProject implements GhidraLaunchable {
    @Override
    public void launch(GhidraApplicationLayout layout, String[] args) throws Exception {
        if (args.length != 4)
            throw new IllegalArgumentException("project-dir project-name rom annotations.json");
        File directory = new File(args[0]);
        if (new File(directory, args[1] + ".gpr").exists())
            throw new IllegalArgumentException("Project exists; use the annotation importer to preserve human work");
        directory.mkdirs();
        JsonNode data = new ObjectMapper().readTree(new File(args[3]));
        byte[] rom = Files.readAllBytes(Path.of(args[2]));
        String md5 = HexFormat.of().formatHex(MessageDigest.getInstance("MD5").digest(rom));
        if (!md5.equals(data.path("executableMd5").asText()))
            throw new IllegalArgumentException("ROM identity does not match annotations");
        HeadlessGhidraApplicationConfiguration configuration = new HeadlessGhidraApplicationConfiguration();
        configuration.setInitializeLogging(false);
        Application.initializeApplication(layout, configuration);
        var language = DefaultLanguageService.getLanguageService().getLanguage(new LanguageID("68000:BE:32:default"));
        GhidraProject project = GhidraProject.createProject(directory.getAbsolutePath(), args[1], false);
        try {
            Program program = project.importProgram(new File(args[2]), language, language.getDefaultCompilerSpec());
            int transaction = program.startTransaction("Verified Sonic disassembly annotations");
            boolean commit = false;
            try {
                FlatProgramAPI api = new FlatProgramAPI(program, TaskMonitor.DUMMY);
                program.getMemory().getBlocks()[0].setExecute(true);
                var ram = program.getAddressFactory().getDefaultAddressSpace().getAddress(0xff0000);
                program.getMemory().createUninitializedBlock("WRAM", ram, 0x10000, false);
                for (JsonNode symbol : data.path("symbols")) {
                    long value = Long.parseUnsignedLong(symbol.path("address").asText(), 16);
                    var address = program.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                    api.createLabel(address, symbol.path("name").asText(), true, SourceType.USER_DEFINED);
                }
                // Vectors are authoritative entry points; labels elsewhere remain labels,
                // avoiding treating every interior disassembly label as a function.
                for (int offset : new int[] {4, 0x70, 0x78}) {
                    long value = ((long)(rom[offset] & 255) << 24) | ((long)(rom[offset+1] & 255) << 16)
                        | ((long)(rom[offset+2] & 255) << 8) | (rom[offset+3] & 255);
                    if (value >= 0x200 && value < rom.length) {
                        var address = program.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                        api.disassemble(address);
                        api.createFunction(address, null);
                    }
                }
                commit = true;
            } finally {
                program.endTransaction(transaction, commit);
            }
            project.saveAs(program, "/", args[1] + ".bin", true);
            System.out.println(args[1] + ": imported " + data.path("symbols").size() + " verified labels; ROM " + md5);
        } finally {
            project.close();
        }
    }
}
