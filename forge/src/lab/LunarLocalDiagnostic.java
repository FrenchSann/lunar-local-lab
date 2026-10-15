package lab;

import java.nio.file.Path;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.common.event.FMLPreInitializationEvent;

@Mod(modid = "lunarlocaldiagnostic", name = "Lunar Local Lab - Diagnostic only",
     version = "0.2.0", clientSideOnly = true, acceptedMinecraftVersions = "[1.8.9]",
     acceptableRemoteVersions = "*")
public final class LunarLocalDiagnostic {
    @Mod.EventHandler
    public void preInit(FMLPreInitializationEvent event) {
        Path directory = event.getModConfigurationDirectory().toPath().resolve("lunar-local-lab");
        try {
            NativeBootstrap.diagnose(directory);
            System.out.println("[Lunar Local Lab] Diagnostic recorded. Gameplay modules remain disabled.");
        } catch (Exception | LinkageError error) {
            // No stack trace or arbitrary exception message from the host.
            System.err.println("[Lunar Local Lab] JNI diagnostic failed: " + error.getClass().getSimpleName());
        }
    }
}
