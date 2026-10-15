# LUNAR / LOCAL LAB

**C++20 · JNI · Lunar Client 1.8.9 · laboratoire local**

Un cœur déterministe pour étudier quatre comportements : répétition sous maintien
physique, maintien du minage, remplacement d'armure et sélection géométrique.
Le cœur simulé est maintenant raccordé à un **adaptateur Minecraft réel**, sans
Forge. La lecture d'état sur le thread du jeu, l'ordonnanceur et l'arrêt ont été
vérifiés dans Lunar. **Les quatre parcours actifs restent à valider en jeu** : le
contexte observé n'était pas admissible et aucun clic/équipement n'a été exécuté.

| Capacité | État du livrable | Dans Lunar |
|---|---|---|
| Répétition à horloge monotone, sans rattrapage | Testée en simulation | Appel réel raccordé ; essai actif non réalisé |
| Maintien contextuel prioritaire sur bloc | Testé en simulation | Préservation du maintien natif implémentée ; essai non réalisé |
| Armure : choix, préconditions, réservation, confirmation, expiration | Testée dans le modèle simplifié | Transaction réelle implémentée, confirmation locale seulement ; essai non réalisé |
| Segment/boîte, distance d'entrée, obstacles, portée | Testé localement | Boîtes et raytrace du jeu raccordés ; essai non réalisé |
| Désarmement et restrictions de contexte | Testés sur instantanés simulés | Lecture réelle et refus d'armement vérifiés |
| Arrêt et barrière de rappels | Testés pour les rappels enregistrés du laboratoire | Déchargement d'un module Lunar non testé |
| JNI : version et attachement/détachement | Diagnostic autonome et diagnostic natif | Testé dans le processus Lunar ; résolution des classes de jeu non vérifiée |
| Injection explicite et lecture mémoire limitée | Compilées et testées sur une JVM de test | Testées sur le processus Lunar, puis DLL déchargée |
| Pont Forge vers JNI | JAR diagnostic compilé, chargé en JVM autonome | Chargement par Lunar non vérifié ; aucune action de jeu |
| Résolution et adaptateur applicatif | Noms/descripteurs relevés dans la JVM | Lecture et ordonnanceur vérifiés ; compatibilité limitée à cette instance |
| Configuration | Paramètres C++ validés et fichier properties | Interface Win32 compilée ; validation visuelle non réalisée |

## Construire et vérifier

Prérequis : CMake ≥ 3.20, compilateur C++20, SDK Windows avec MSVC pour le preset fourni.
Aucune dépendance C++ tierce, aucun téléchargement de binaire d'injection.

```powershell
cmake --preset windows-msvc
cmake --build --preset release
ctest --preset release -V
```

Diagnostic JNI **dans son propre processus Java**, avec un JDK x64 installé :

```powershell
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-21.0.6.7-hotspot'
cmake --preset windows-msvc -DLUNAR_LAB_JNI_PROBE=ON
cmake --build --preset release
ctest --preset release -V
```

Adapter le chemin à son JDK. `-Xcheck:jni` est activé par CTest. Le programme charge
uniquement la DLL compilée ici, via `System.load`, dans la JVM de test.
Pour désactiver ce diagnostic dans un répertoire déjà configuré :
`cmake --preset windows-msvc -DLUNAR_LAB_JNI_PROBE=OFF`.

Compilation portable du cœur (sans JNI) :

```sh
cmake -S . -B build -DLUNAR_LAB_JNI_PROBE=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Règles du laboratoire

- Après chaque changement de contexte, appeler `tick` puis réarmer explicitement avec `arm`.
- Depuis 0.3.1, les mondes solo et les serveurs distants sont admissibles. Le monde doit être actif, la fenêtre focalisée et aucun menu ouvert.
- Une reconnexion ou un changement de monde/joueur/éligibilité désarme les modules. F8 permet de réarmer.
- `syntheticLeft` ne déclenche pas de répétition. L'adaptateur lit le bouton
  via LWJGL et émet ses actions par appels applicatifs, sans simulation de bouton OS.
- Les actions retournées sont des intentions consommées sur le thread applicatif.
  Le pair et le contexte sont vérifiés de nouveau avant l'appel de clic/inventaire.
- Un `EndMining` d'ancien contexte libère uniquement la réservation possédée de
  cet ancien contexte. Il ne doit jamais modifier le nouveau monde. Le modèle
  ne synthétise aucun bouton système.

La restriction de connexion locale a été retirée à la demande de l'utilisateur en 0.3.1.
Aucune protection n'est désactivée. Aucun mécanisme de furtivité n'est prévu.

## Lire les preuves et les limites

- [Compatibilité observée et blocages](docs/COMPATIBILITY.md)
- [Architecture et contrats d'intégration](docs/ARCHITECTURE.md)
- [Résultats réellement obtenus](docs/TEST_RESULTS.md)
- [Construire et charger le diagnostic Forge](docs/FORGE_DIAGNOSTIC.md)
- [Diagnostic natif injecté sans Forge : résultats et commandes](docs/PROCESS_DIAGNOSTIC.md)
- **[Adaptateur Minecraft réel : activation, configuration et limites](docs/GAME_ADAPTER.md)**

Le classement d'armure compare protection, score heuristique d'attributs,
durabilité relative puis absolue, et identifiant stable. Ce n'est pas un calcul
validé des dégâts Minecraft. La confirmation serveur n'est pas observée directement.

La portée contrôle une sélection géométrique locale ; elle n'augmente pas la portée
acceptée par un serveur. Le modèle utilise des boîtes ; l'adaptateur utilise
également le raytrace de blocs du jeu pour limiter la visibilité.

## Interface Windows

L'executable natif `lunar_control.exe` permet de configurer la portee, la cadence et l'armure, puis de piloter le pont. Voir [le guide](docs/CONTROL_PANEL.md).
