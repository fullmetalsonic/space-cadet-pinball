package com.codex.spacecadetpinball;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

/** Installs only packaged game/font/music content; never writes game preferences or scores. */
final class AssetInstaller {
    interface Source { InputStream open(String path) throws IOException; }
    static final String MANIFEST = "pinball-assets.sha256";
    static File install(File root, Source source) throws IOException {
        byte[] manifest;
        try (InputStream input=source.open(MANIFEST)) { manifest=readAll(input); }
        String version=hash(manifest);
        Map<String,String> entries=parse(manifest);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create content directory");
        File target=new File(root,version);
        if (verify(target,entries)) return target;
        File stage=new File(root,"stage-"+version+"-"+UUID.randomUUID());
        if (!stage.mkdir()) throw new IOException("Cannot create content staging directory");
        for (Map.Entry<String,String> entry:entries.entrySet()) {
            File output=safeFile(stage,"assets/"+entry.getKey());
            if (!output.getParentFile().isDirectory() && !output.getParentFile().mkdirs()) throw new IOException("Cannot create asset directory");
            try(InputStream input=source.open(entry.getKey()); FileOutputStream file=new FileOutputStream(output)) {
                byte[] buffer=new byte[65536]; int count;
                while ((count=input.read(buffer))!=-1) file.write(buffer,0,count);
                file.getFD().sync();
            }
        }
        if (!verify(stage,entries)) throw new IOException("Packaged content verification failed");
        if (target.exists()) {
            File damaged=new File(root,"damaged-"+version+"-"+UUID.randomUUID());
            if (!target.renameTo(damaged)) throw new IOException("Cannot preserve damaged content cache");
        }
        if (!stage.renameTo(target)) throw new IOException("Cannot publish verified content");
        if (!verify(target,entries)) throw new IOException("Installed content verification failed");
        return target;
    }
    private static Map<String,String> parse(byte[] bytes) throws IOException {
        Map<String,String> entries=new LinkedHashMap<>();
        for(String line:new String(bytes,StandardCharsets.UTF_8).split("\n")) {
            if(line.isEmpty()) continue;
            String[] pair=line.split("\t",-1);
            if(pair.length!=2 || !pair[0].matches("[0-9a-f]{64}") || !(pair[1].startsWith("game/")||pair[1].startsWith("fonts/")||pair[1].startsWith("audio/")) || pair[1].contains("..") || pair[1].contains("\\") || pair[1].contains(":")) throw new IOException("Invalid content manifest");
            if(entries.put(pair[1],pair[0])!=null) throw new IOException("Duplicate content entry");
        }
        if(entries.isEmpty() || !entries.containsKey("fonts/NotoSansKR.ttf") || !entries.containsKey("audio/TimGM6mb.sf2") || entries.keySet().stream().noneMatch(p->p.startsWith("game/"))) throw new IOException("Required content missing");
        return entries;
    }
    private static boolean verify(File root,Map<String,String> entries) throws IOException {
        if(!root.isDirectory()) return false;
        for(Map.Entry<String,String> entry:entries.entrySet()) {
            File file=safeFile(root,"assets/"+entry.getKey());
            if(!file.isFile()) return false;
            try(InputStream input=new FileInputStream(file)) { if(!entry.getValue().equals(hash(input))) return false; }
        }
        return true;
    }
    private static File safeFile(File root,String path) throws IOException {
        File result=new File(root,path).getCanonicalFile();
        if(!result.getPath().startsWith(root.getCanonicalPath()+File.separator)) throw new IOException("Content path escapes its directory");
        return result;
    }
    private static byte[] readAll(InputStream input) throws IOException {
        ByteArrayOutputStream output=new ByteArrayOutputStream(); byte[] buffer=new byte[8192]; int count;
        while((count=input.read(buffer))!=-1) { if(output.size()+count>256*1024) throw new IOException("Content manifest too large"); output.write(buffer,0,count); }
        return output.toByteArray();
    }
    static String hash(byte[] bytes) throws IOException { return hash(new ByteArrayInputStream(bytes)); }
    private static String hash(InputStream input) throws IOException {
        try {
            MessageDigest digest=MessageDigest.getInstance("SHA-256"); byte[] buffer=new byte[65536]; int count;
            while((count=input.read(buffer))!=-1) digest.update(buffer,0,count);
            StringBuilder result=new StringBuilder(); for(byte b:digest.digest()) result.append(String.format(Locale.ROOT,"%02x",b&255)); return result.toString();
        } catch(java.security.NoSuchAlgorithmException impossible) { throw new IOException(impossible); }
    }
}
