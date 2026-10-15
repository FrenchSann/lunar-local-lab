package lab;

/** Standalone lab JVM. This is not a Lunar mod or an injection mechanism. */
public final class Probe {
    private static native int run(ClassLoader loader);
    /** Called only after the host explicitly loaded this project's native library. */
    public static int diagnose() { return run(Probe.class.getClassLoader()); }
    public static void main(String[] args) {
        if (args.length != 1) throw new IllegalArgumentException("Absolute library path required");
        System.load(args[0]);
        for (int i = 0; i < 20; i++) {
            int result = diagnose();
            if (result != 63) throw new AssertionError("JNI diagnostic bits: " + result);
        }
        System.out.println("JNI standalone: 20 passes; attachment ownership, loader, references and expected exception checked");
        System.out.println("Runtime: " + System.getProperty("java.version") + " / " + System.getProperty("os.arch"));
    }
}
