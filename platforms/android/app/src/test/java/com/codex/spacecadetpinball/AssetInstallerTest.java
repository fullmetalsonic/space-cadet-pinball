package com.codex.spacecadetpinball;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import org.junit.Test;
import static org.junit.Assert.*;

public class AssetInstallerTest {
    private Map<String,byte[]> fixture() throws IOException {
        Map<String,byte[]> files=new LinkedHashMap<>(); files.put("game/PINBALL.DAT",new byte[]{1,2,3}); files.put("fonts/NotoSansKR.ttf",new byte[]{4,5,6}); files.put("audio/TimGM6mb.sf2",new byte[]{7,8});
        StringBuilder manifest=new StringBuilder(); for(Map.Entry<String,byte[]> e:files.entrySet()) manifest.append(AssetInstaller.hash(e.getValue())).append('\t').append(e.getKey()).append('\n');
        files.put(AssetInstaller.MANIFEST,manifest.toString().getBytes(StandardCharsets.UTF_8)); return files;
    }
    private AssetInstaller.Source source(Map<String,byte[]> files) { return path->{ if(!files.containsKey(path))throw new IOException("Missing packaged asset"); return new ByteArrayInputStream(files.get(path)); }; }
    @Test public void installsVerifiedReusesValidCacheAndRepairsDamagedCacheWithoutTouchingScores() throws Exception {
        File parent=Files.createTempDirectory("pinball-content-test").toFile(); File score=new File(parent,"scores.ini"); Files.write(score.toPath(),new byte[]{9});
        File root=new File(parent,"content"); Map<String,byte[]> files=fixture(); File first=AssetInstaller.install(root,source(files));
        assertArrayEquals(files.get("game/PINBALL.DAT"),Files.readAllBytes(new File(first,"assets/game/PINBALL.DAT").toPath()));
        assertEquals(first,AssetInstaller.install(root,source(files)));
        Files.write(new File(first,"assets/game/PINBALL.DAT").toPath(),new byte[]{0});
        assertEquals(first,AssetInstaller.install(root,source(files)));
        assertArrayEquals(files.get("game/PINBALL.DAT"),Files.readAllBytes(new File(first,"assets/game/PINBALL.DAT").toPath())); assertArrayEquals(new byte[]{9},Files.readAllBytes(score.toPath()));
        assertTrue(Arrays.stream(root.list()).anyMatch(n->n.startsWith("damaged-")));
    }
    @Test public void partialCopyNeverBecomesReusableVersion() throws Exception {
        File root=Files.createTempDirectory("pinball-partial-test").toFile(); Map<String,byte[]> files=fixture(); files.remove("fonts/NotoSansKR.ttf");
        try { AssetInstaller.install(root,source(files)); fail("missing asset must fail"); } catch(IOException expected) { }
        assertTrue(Arrays.stream(root.list()).allMatch(n->n.startsWith("stage-")));
    }
    @Test public void pathTraversalRejectedBeforeCreatingContent() throws Exception {
        Map<String,byte[]> files=fixture(); String manifest=new String(files.get(AssetInstaller.MANIFEST),StandardCharsets.UTF_8); manifest+=AssetInstaller.hash(new byte[]{1})+"\tgame/../../scores.ini\n"; files.put(AssetInstaller.MANIFEST,manifest.getBytes(StandardCharsets.UTF_8));
        File root=new File(Files.createTempDirectory("pinball-path-test").toFile(),"content");
        try { AssetInstaller.install(root,source(files)); fail("unsafe manifest must fail"); } catch(IOException expected) { }
        assertFalse(root.exists());
    }
    @Test public void mandatoryMusicBankMissingFromManifestCannotStartGame() throws Exception {
        Map<String,byte[]> files=fixture(); String manifest=new String(files.get(AssetInstaller.MANIFEST),StandardCharsets.UTF_8);
        manifest=manifest.replaceAll("(?m)^.*audio/TimGM6mb\\.sf2\\n",""); files.put(AssetInstaller.MANIFEST,manifest.getBytes(StandardCharsets.UTF_8));
        File root=new File(Files.createTempDirectory("pinball-music-test").toFile(),"content");
        try { AssetInstaller.install(root,source(files)); fail("missing music bank must fail"); } catch(IOException expected) { }
        assertFalse(root.exists());
    }
}
