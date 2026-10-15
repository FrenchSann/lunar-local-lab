# Diagnostic natif explicite — sans Forge

À la demande explicite de l'utilisateur, le projet comporte maintenant un mode
d'injection Windows x64. Il remplace, pour ce diagnostic uniquement, le besoin
d'un point d'entrée Forge. Les constats antérieurs décrivaient l'état du projet
avant cette nouvelle autorisation.

## Ce qui a réellement été exécuté

Le 16 octobre 2026, sur le processus `javaw` de l'installation Lunar, PID **12836** :

```text
ReadProcessMemory verified jvm.dll MZ/PE, machine=0x8664, imageSize=0xc74000
Injected diagnostic completed: VM count=1, JNI=0xa0000,
MZ=0x5a4d, PE=0x4550, attachedHere=1, detached=1.
DLL unloaded after thread completion.
```

Le chemin de l'exécutable désignait le JRE Lunar **Zulu 17.0.18 x64**.
`JNI=0xa0000` est la version d'interface rapportée par `JNIEnv::GetVersion`,
pas une preuve que le runtime serait Java 10 ni une identification de la build du jeu.
Après l'opération : processus toujours présent, `Responding=true` et aucun module
`lunar_memory_probe.dll` restant. Cela ne constitue pas un test visuel du jeu.

## Portée de la lecture

Le programme lit les en-têtes DOS/PE des modules concernés et une structure de
résultat de 36 octets qu'il a lui-même réservée. Il ne parcourt pas le tas Java,
ne recherche aucun secret, ne lit pas de jeton et ne modifie aucun objet de jeu.
`WriteProcessMemory` écrit seulement le chemin de notre DLL dans une allocation
créée pour ce diagnostic. La DLL remplit uniquement sa structure de résultat.

Le diagnostic utilise les API Windows documentées
[ReadProcessMemory](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory)
et [CreateRemoteThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createremotethread),
puis l'[API d'invocation JNI](https://docs.oracle.com/en/java/javase/17/docs/specs/jni/invocation.html).
Il n'active aucun privilège supplémentaire et n'essaie pas de contourner un refus d'accès.

## Compilation et usage reproductible

```powershell
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-21.0.6.7-hotspot'
cmake --preset windows-msvc -DLUNAR_LAB_JNI_PROBE=ON -DLUNAR_LAB_PROCESS_DIAGNOSTIC=ON
cmake --build --preset release
.\scripts\Test-ProcessDiagnostic.ps1 -Jdk $env:JAVA_HOME
```

Pour une instance Java personnelle, remplacer `PID_ACTUEL` par son PID vérifié :

```powershell
.\build\Release\inspect_process.exe PID_ACTUEL --read-header
.\build\Release\inspect_process.exe PID_ACTUEL --inject-diagnostic
```

Le PID 12836 est uniquement celui de l'essai : ne pas le réutiliser à l'aveugle.
L'exécutable exige Java x64 et la présence de `jvm.dll`. Il charge exclusivement
`lunar_memory_probe.dll` située à côté de lui ; il n'accepte pas un chemin de DLL arbitraire.
Pour distribuer le diagnostic, conserver ces deux fichiers dans le même dossier.

## Cycle de vie et erreurs

L'adresse d'un export système est résolue relativement à son module propriétaire,
y compris lorsqu'il est transféré de Kernel32 vers KernelBase. Le chemin, la taille
d'image et l'horodatage PE des modules système locaux/distants sont comparés ;
aucune égalité d'adresses entre processus n'est supposée.

Le chargement appelle `LoadLibraryW`. Le module est ensuite retrouvé dans la liste
des modules du processus, sans tronquer son adresse en un code retour 32 bits.
Son export est appelé seulement après vérification du module et de son RVA.
Ces vérifications sont des contrôles de cohérence, pas une signature cryptographique.

Le diagnostic ne travaille pas dans DllMain et ne crée aucun worker persistant.
Il obtient le JavaVM existant, attache uniquement son thread si nécessaire,
interroge la version JNI et détache uniquement ce qu'il a attaché.
`FreeLibrary` n'est appelé qu'après terminaison observée du thread de diagnostic.
L'absence du module est vérifiée après cette libération.

Si un thread ne termine pas en 10 secondes, aucune terminaison forcée ou libération
de mémoire encore utilisée n'est tentée : l'allocation et/ou la DLL restent en place
jusqu'à la sortie du processus. Certaines erreurs après chargement laissent également
la DLL résidente. Le programme signale l'échec ; il ne prétend pas avoir tout nettoyé.
L'injection peut perturber le timing ou bloquer un processus : le diagnostic est
ponctuel, et ses tests ne prouvent pas une absence de risque pour toute build.

## État des quatre modules

L'injection est vérifiée ; l'accès aux objets Minecraft ne l'est pas. Les quatre
fonctions restent dans le cœur simulé. Il manque toujours la build exacte du jeu,
les correspondances valides, le ClassLoader applicatif, les instantanés de jeu,
le thread applicatif et les opérations d'inventaire confirmées. Une lecture d'en-tête
de module n'établit aucune de ces correspondances.
