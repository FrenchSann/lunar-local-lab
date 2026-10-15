package lab;

import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.Properties;

/** Read-only JVM diagnostics. Never reads authentication properties or launch arguments. */
public final class NativeBootstrap {
    private static boolean loaded;
    private NativeBootstrap() {}

    private static String sha256(byte[] bytes) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
        StringBuilder text = new StringBuilder();
        for (byte b : digest) text.append(String.format("%02x", b & 255));
        return text.toString();
    }

    public static synchronized Properties diagnose(Path directory) throws Exception {
        Properties status = new Properties();
        status.setProperty("target", "Lunar Client 1.8.9");
        status.setProperty("gameplay", "disabled: application adapter and mappings unverified");
        status.setProperty("java.version", System.getProperty("java.version", "unknown"));
        status.setProperty("os.arch", System.getProperty("os.arch", "unknown"));
        status.setProperty("client.build", "unverified");
        status.setProperty("loader", "caller-supplied class loader; not proof of Lunar integration");
        String arch = System.getProperty("os.arch", "");
        if (!System.getProperty("os.name", "").startsWith("Windows") || !(arch.equals("amd64") || arch.equals("x86_64"))) {
            status.setProperty("jni", "blocked: requires Windows x64");
        } else {
            if (!loaded) {
                Properties manifest = new Properties();
                try (InputStream in = NativeBootstrap.class.getResourceAsStream("/native/library.properties")) {
                    if (in == null) throw new IllegalStateException("Missing native manifest");
                    manifest.load(in);
                }
                byte[] bytes;
                try (InputStream in = NativeBootstrap.class.getResourceAsStream("/native/jni_probe.dll")) {
                    if (in == null) throw new IllegalStateException("Missing native library");
                    java.io.ByteArrayOutputStream buffer = new java.io.ByteArrayOutputStream();
                    byte[] chunk = new byte[8192]; int n;
                    while ((n = in.read(chunk)) != -1) buffer.write(chunk, 0, n);
                    bytes = buffer.toByteArray();
                }
                String hash = sha256(bytes);
                if (!hash.equals(manifest.getProperty("sha256"))) throw new SecurityException("Native checksum mismatch");
                Path nativeDir = directory.resolve("native").resolve(hash);
                Files.createDirectories(nativeDir);
                Path dll = nativeDir.resolve("jni_probe.dll");
                if (Files.exists(dll)) {
                    if (!hash.equals(sha256(Files.readAllBytes(dll)))) throw new SecurityException("Existing DLL checksum mismatch");
                } else Files.write(dll, bytes, java.nio.file.StandardOpenOption.CREATE_NEW);
                System.load(dll.toAbsolutePath().toString());
                loaded = true;
            }
            int bits = Probe.diagnose();
            status.setProperty("jni", bits == 63 ? "passed" : "failed");
            status.setProperty("jni.bits", Integer.toString(bits));
        }
        Files.createDirectories(directory);
        try (OutputStream out = Files.newOutputStream(directory.resolve("diagnostic.properties"))) {
            status.store(out, "Lunar Local Lab: diagnostic only, no gameplay integration");
        }
        return status;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Diagnostic output directory required");
        Properties result = diagnose(Paths.get(args[0]));
        if (!"passed".equals(result.getProperty("jni"))) throw new AssertionError("Packaged JNI diagnostic failed");
        System.out.println("Packaged JNI passed on Java " + result.getProperty("java.version") + "; gameplay disabled");
    }
}
