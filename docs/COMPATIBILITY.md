# Compatibilité — constat du 16 octobre 2026

Cible explicite : Lunar Client 1.8.9, instances personnelles et serveur local.

| Élément | Observation en lecture seule |
|---|---|
| Dépôt initial | Espace de travail vide (outputs/ et work/), aucun AGENTS.md trouvé dans le dossier ou ses parents inspectés |
| Système | Windows 11 Professionnel 10.0.26200, 64 bits |
| Compilation | Visual Studio Community 2022, MSVC 14.43.34808, CMake 4.0.0 ; Ninja présent dans Visual Studio |
| Java du PATH | Eclipse Adoptium JDK 21.0.6.7-hotspot ; ne prouve pas le Java utilisé par Lunar |
| JRE Lunar présents | Zulu 17.0.3 et 21.0.1, x86_64 (fichiers release) |
| Java réellement utilisé | Indéterminé : aucun processus java/javaw/lunar observé lors du diagnostic |
| Installation | ~/.lunarclient/offline/multiver ; profils 1.8 et 1.21 présents |
| JAR observé | offline/multiver/lunar.jar ; manifeste limité à Manifest-Version: 1.0 |
| SHA-1 local | 939b948894ee9d4a63d09e087aa3e57f4f429485 |
| Build exacte | Non établie par les métadonnées inspectées ; le nom du profil ne suffit pas |
| Mappings locaux | Aucun fichier Tiny trouvé ; les fichiers tzMappings des JRE ne sont pas des mappings Minecraft |

La [publication a397868](https://github.com/Lunar-Mapping-Project/mappings/releases/tag/a397868)
vise **1.8.9-a397868/master**, au format Tiny V2, et annonce l'empreinte JAR
`56b65a5e24e521d47ea44d07eee75922c9fcc2c0`. Le JAR local ne correspond pas.
Cette comparaison ne permet pas d'identifier sa build ; elle interdit de considérer
les mappings de référence comme validés. Aucun mapping, offset ou descripteur client n'est inventé.

Le [récapitulatif officiel de septembre 2026](https://www.lunarclient.com/news/lunar-client-monthly-recap-september-2026)
annonce le chargement de mods Forge personnalisés pour 1.8.9 et 1.12.2.
Cette possibilité documentaire n'établit ni sa présence dans cette installation,
ni un contrat de chargement natif vérifié. Aucun chargeur Forge correspondant
n'a été trouvé par la recherche de noms de fichiers. Aucun client n'a été démarré ou modifié.

## Blocages précis pour l'intégration

1. Instance Lunar 1.8.9 active avec build et JVM attestées (sans exposer ses arguments d'authentification).
2. Mappings correspondant au JAR effectivement chargé, avec provenance et validation des descripteurs.
3. Point d'entrée officiellement pris en charge dans cette installation, capable de charger notre composant.
4. Contrat du thread applicatif, chargeur de classes, événements de contexte, connexion directe et pair réseau.
5. Adaptateur vérifié pour entrée physique, rayon, inventaire, confirmations et opérations applicatives.

JNI ne charge pas un composant dans un autre processus. Aucune injection,
modification de protection, furtivité ou recherche de contournement n'est réalisée.
Les journaux bruts, identifiants de session, fichiers client et binaires propriétaires
ne sont pas copiés dans ce dépôt. L'absence d'un mécanisme vérifié bloque l'intégration,
pas les simulations indépendantes.

## Réinspection pendant la deuxième passe

Au début de la passe, le launcher installé annonçait **3.3.6.0**. Le fichier
`profiles/lunar/1.8/profile.json` indiquait `gameVersion=1.8.9`, `loaders=[ichor]`,
`type=lunar`, `lunar.module=lunar`. Son dossier mods était vide. Ces champs ont été
lus sélectivement sans publier l'identifiant ou le nom du profil.

Pendant le travail, un exécutable de mise à jour **3.7.24-ow** puis le launcher
**3.7.24.0** ont été observés. Les profils ont changé d'emplacement vers
`profiles/1.8` et `profiles/1.21` ; l'ancien fichier profile.json a disparu.
Ce changement a été observé, pas déclenché par les scripts du dépôt. Aucun processus
Java de jeu n'a été observé lors de la dernière inspection de cette passe.
La version du launcher n'est pas la build exacte du client Minecraft.

Un pont diagnostic Forge a été compilé contre le JAR officiel 1.8.9. Il n'a pas été
copié dans un dossier mods dont le chargeur effectif n'est pas établi. Le dossier
de destination doit être celui indiqué par l'interface officielle pour le profil
Forge 1.8.9. L'installation et le chargement dans Lunar restent non vérifiés.

`scripts/Inspect-Lunar.ps1` permet de refaire un diagnostic limité sans afficher
les arguments de processus ou les fichiers de session.

## Troisième passe : processus actif et diagnostic injecté

Après nouvelle autorisation explicite de l'utilisateur, le diagnostic natif a été
compilé puis injecté avec succès dans le processus Lunar `javaw` PID 12836.
Le runtime observé est maintenant Zulu **17.0.18 x64**, distinct des anciens JRE
présents sur disque. Les en-têtes de `jvm.dll` ont réellement été lus avec
ReadProcessMemory et l'API JNI interrogée dans ce processus.

L'empreinte SHA-1 du fichier `offline/multiver/lunar.jar` observé à cette passe est
`c98eaedfee35eaa86b1a6bdd09c3c4b6251b57fc`. Elle diffère encore de la référence
a397868. Cette empreinte du fichier sur disque ne prouve pas à elle seule quels
octets de classes la JVM a effectivement chargés.

Le blocage « aucun chargement disponible » est levé pour le diagnostic natif.
Le blocage sur les objets de jeu, mappings, thread applicatif et quatre capacités
reste ouvert. Voir [le rapport d'exécution](PROCESS_DIAGNOSTIC.md).

## Quatrième passe : classes nommées et adaptateur applicatif

Le fichier `lunar-platform-mappings-v1_8.jar` est maintenant présent et contient
des entrées `.kin`, notamment `lunar/lunar_named_b5_1.8.9.kin`. Il n'a pas été
appliqué comme table d'offsets ni distribué. L'inventaire JVMTI de la JVM active
expose directement les classes nommées `net/minecraft/...` : les signatures
réellement chargées ont servi à valider les accès.

Les signatures vérifiées incluent `Minecraft.getMinecraft`, `addScheduledTask`
(Callable et Runnable), `isCallingFromMinecraftThread`, `clickMouse`,
`PlayerControllerMP.windowClick`, `NetworkManager.getRemoteAddress`,
`ItemStack.areItemStacksEqual`, les slots et les boîtes des entités.
La lecture d'état et l'ordonnancement dans Minecraft ont réussi. Le premier état
lu indiquait monde présent, menu ouvert, absence de focus et pair non reconnu
comme boucle locale. L'armement a été refusé sans action de jeu.

L'adaptateur est implémenté pour les noms/descripteurs observés dans cette instance,
mais la build commerciale exacte n'est toujours pas attestée. Les quatre actions
actives demandent encore une validation en jeu. Le chargement des mêmes noms dans
une autre build ne prouve pas à lui seul des comportements identiques.
