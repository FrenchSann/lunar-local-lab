# Pont Forge → JNI : diagnostic uniquement

Le mod `lunarlocaldiagnostic` est compilé contre les véritables annotations et
événements de Forge **1.8.9-11.15.1.2318-1.8.9**. Les signatures ont été inspectées
avec `javap` dans le JAR officiel ; aucune classe Minecraft obfusquée n'est utilisée.
Il ne nécessite donc pas les mappings Lunar `a397868` pour son seul diagnostic.

## Construire

Télécharger la dépendance de compilation depuis le
[Maven officiel Forge](https://maven.minecraftforge.net/net/minecraftforge/forge/1.8.9-11.15.1.2318-1.8.9/forge-1.8.9-11.15.1.2318-1.8.9-universal.jar).
La [page officielle](https://files.minecraftforge.net/net/minecraftforge/forge/index_1.8.9.html)
publie son SHA-1 : `beda619c465af293e63952dd573c137c17c0a4cd`.
Cette dépendance n'est ni un injecteur ni embarquée dans le livrable ; elle sert de
classpath de compilation. Le script vérifie son empreinte avant compilation.

Après la compilation CMake avec JNI activé :

```powershell
.\scripts\Build-ForgeDiagnostic.ps1 `
  -Jdk 'C:\Program Files\Eclipse Adoptium\jdk-21.0.6.7-hotspot' `
  -ForgeJar 'CHEMIN\forge-1.8.9-11.15.1.2318-1.8.9-universal.jar'
```

Le JDK de compilation doit être ≥ 9 ; `--release 8` produit du bytecode Java 8.
Le résultat est `dist/lunar-local-diagnostic-0.2.0.jar`, contenant la DLL compilée
ici et une empreinte SHA-256 de contrôle d'intégrité. Ce contrôle détecte une
incohérence de paquet ; il ne constitue pas une signature d'éditeur.

## Charger par le mécanisme officiel

Utiliser uniquement un profil Lunar **1.8.9 autorisant les mods Forge** et l'interface
officielle d'ajout de mods. L'existence d'un dossier `mods` ou d'un profil `ichor`
ne prouve pas cette capacité. Aucun script ne remplace automatiquement son chargeur.
Le [récapitulatif Lunar de septembre 2026](https://www.lunarclient.com/news/lunar-client-monthly-recap-september-2026)
annonce ce mécanisme ; sa présence effective reste à constater dans le launcher.

À l'événement Forge `FMLPreInitializationEvent`, le mod :

1. Vérifie Windows x64 et l'intégrité de sa bibliothèque embarquée.
2. Place sa DLL dans `config/lunar-local-lab/native/<sha256>/jni_probe.dll`.
3. La charge explicitement via `System.load` dans sa propre JVM hôte.
4. Exécute le diagnostic de ClassLoader et d'attachement JNI.
5. Écrit uniquement des champs autorisés dans `config/lunar-local-lab/diagnostic.properties`.

Il ne lit ni arguments de lancement, ni jetons, ni identifiants de session. Il ne
modifie aucun inventaire, entrée, cible ou connexion. Il ne charge pas de DLL dans
un processus extérieur. Aucun worker ne subsiste après le diagnostic.
Le composant reste chargé jusqu'à la fin de la JVM, sans tentative de déchargement.

## Vérification hors client

```powershell
& "$env:JAVA_HOME\bin\java.exe" -Xcheck:jni `
  -cp dist\lunar-local-diagnostic-0.2.0.jar `
  lab.NativeBootstrap build\packaged-smoke
```

Ce test exerce l'extraction, le contrôle d'intégrité et le chargement du même JAR.
Pour vérifier également le rechargement dans une nouvelle JVM et le refus d'une
copie de test corrompue : `scripts/Test-ForgeDiagnostic.ps1 -Java CHEMIN\java.exe`.
Il ne vérifie pas que Forge découvre le mod dans Lunar. L'événement Forge n'a pas
été observé dans le client à ce stade. Le diagnostic désigne explicitement la
build client et l'adaptateur comme non vérifiés.

**Les quatre fonctions restent en simulation.** Le prochain jalon est la preuve
du chargement de ce diagnostic dans Lunar ; ensuite seulement viennent les
instantanés applicatifs, la restriction aux connexions directes sur boucle locale,
les mappings effectivement nécessaires et les actions intégrées.
