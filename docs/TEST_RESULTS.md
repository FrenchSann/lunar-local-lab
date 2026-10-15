# Résultats exécutés — 16 octobre 2026

Compilation locale x64 Release réussie avec MSVC **19.43.34809.0**, toolset
14.43.34808, Windows SDK 10.0.22621.0, CMake 4.0.0.

Commande : `ctest --preset release -V`, après compilation avec le diagnostic JNI activé.

| Suite | Résultat observé |
|---|---|
| context | 25 vérifications réussies |
| scheduler | 20 vérifications réussies |
| armor | 22 vérifications réussies |
| geometry | 13 vérifications réussies |
| runtime | 4 vérifications réussies |
| jni_standalone | 20 passages réussis avec `-Xcheck:jni`, Java 21.0.6 / amd64 |

**6/6 suites réussies, 0 échec ; durée CTest observée : 0,72 s.**
Les assertions C++ utilisent des exceptions et restent actives en Release.

Couverture : invalidation du contexte et des révisions d'action, refus d'adresses
non admises, séparation physique/synthétique, délais limites et grand retard,
maintien/reprise/libération du minage, arrêt idempotent, priorité au minage,
curseur occupé, choix d'armure, expiration sans retry immédiat, confirmation
tardive, changement de contexte pendant transaction, intersections parallèles,
tangentes et inversées, départ intérieur, obstacles et égalités, entrées invalides,
rappel en vol et attente d'arrêt sans rappel actif.

Le diagnostic JNI vérifie : thread initialement détaché, résolution par ClassLoader,
exception attendue et effacée, détachement du thread créé, conservation de
l'attachement du thread Java appelant. Ses références locales sont encadrées
et la référence globale supprimée après jonction du worker.

Ce ne sont ni des tests d'intégration Lunar, ni des tests serveur, ni une preuve
d'absence de fuite sur longue durée. Aucun injecteur, paquet réseau, clic système
ou mutation d'inventaire réelle n'est exécuté. La CI GitHub est configurée ; son
résultat distant est distinct de ces résultats locaux.

## Deuxième passe : paquet Forge/JNI

Recompilation Release effectuée après ajout de la compatibilité bytecode Java 8.
CTest : **6/6 réussies**, 84 vérifications C++ et 20 passages JNI, durée 0,77 s.
Le compilateur Java 21 signale que la cible Java 8 est ancienne ; ce choix est
intentionnel pour le client ciblé.

Le JAR `lunar-local-diagnostic-0.2.0.jar` a été compilé contre Forge officiel, puis
chargé via son point d'entrée autonome `lab.NativeBootstrap` avec `-Xcheck:jni` :

| JVM réellement exécutée pour ce test | Résultat |
|---|---|
| Oracle 1.8.0_202 x64 | Code retour 0, diagnostic réussi, avertissements JNI présents |
| Zulu 17.0.3 x64 livré dans l'installation Lunar | Code retour 0, diagnostic réussi, aucun avertissement observé |
| Adoptium 21.0.6 x64 | Code retour 0, diagnostic réussi, aucun avertissement observé |

Sur Java 8, un avertissement de capacité de références locales apparaît aussi avec
`java -Xcheck:jni -version`, avant notre bibliothèque. Un autre avertissement sur
`CallStaticObjectMethod` apparaît pendant le test du paquet ; son origine n'est pas
établie. Ce test n'est donc pas qualifié d'exempt d'avertissements sur Java 8.

La première CI distante a échoué : `windows-latest` ne fournissait plus le générateur
Visual Studio 2022 demandé. Le workflow est corrigé pour utiliser `windows-2022`.
La [CI du correctif](https://github.com/FrenchSann/lunar-local-lab/actions/runs/38815960881)
a ensuite terminé avec succès.

Le script `Test-ForgeDiagnostic.ps1` a également réussi sur Java 21.0.6 : premier
chargement, réutilisation de la DLL dans une nouvelle JVM, puis rejet explicite
d'une copie de test corrompue avant `System.load`.

SHA-256 du JAR diagnostic produit lors de cette passe :
`7e7d1d346c2ef6dcfb6d5a94bc44d871505ed1268e9854bb49f010c3fea250e5`.
Un nouveau packaging peut changer ce SHA-256, notamment à cause des horodatages ZIP.

## Troisième passe : injection et lecture mémoire

Compilation x64 Release de `inspect_process.exe` et `lunar_memory_probe.dll` réussie.
`Test-ProcessDiagnostic.ps1` a réussi sur une JVM Java 21.0.6 créée pour le test :
lecture mémoire externe, deux cycles chargement/diagnostic/déchargement et refus
d'un processus non Java. Seul le processus de test créé par le script est arrêté
à la fin ; l'instance Lunar n'est pas arrêtée.

Un cycle a ensuite réussi sur le processus Lunar PID 12836, Zulu 17.0.18 x64.
Une JVM détectée, JNI `0xa0000`, signatures MZ `0x5a4d` et PE `0x4550`,
thread attaché puis détaché et absence de la DLL après déchargement vérifiées.
Après l'opération, processus vivant et répondant selon Windows.

Les six suites CTest existantes ont ensuite été réexécutées : **6/6 réussies,
0 échec**, durée observée **1,75 s**. Les cas timeout/refus d'accès/architecture
incompatible ne sont pas couverts par un essai réel dans cette passe.
