# LUNAR / LOCAL LAB

**C++20 · JNI · Lunar Client 1.8.9 · laboratoire local**

Un cœur déterministe pour étudier quatre comportements : répétition sous maintien
physique, maintien du minage, remplacement d'armure et sélection géométrique.
Les simulations sont exécutables. **Aucune fonction n'est intégrée au client Lunar.**

| Capacité | État du livrable | Dans Lunar |
|---|---|---|
| Répétition à horloge monotone, sans rattrapage | Implémentée et testée en simulation | Bloquée |
| Maintien contextuel prioritaire sur bloc | Implémenté et testé en simulation | Bloqué |
| Armure : choix, préconditions, réservation, confirmation, expiration | Implémenté et testé dans le modèle simplifié | Bloqué |
| Segment/boîte, distance d'entrée, obstacles, portée | Implémenté et testé localement | Bloqué ; aucune validation serveur |
| Désarmement lors d'un changement de contexte | Implémenté et testé sur instantanés simulés | Observation réseau réelle manquante |
| Arrêt et barrière de rappels | Testés pour les rappels enregistrés du laboratoire | Déchargement d'un module Lunar non testé |
| JNI : attachement, chargeur, références, exception attendue | Diagnostic autonome ; voir résultats | Bloqué dans Lunar |
| Pont Forge vers JNI | JAR diagnostic compilé, chargé en JVM autonome | Chargement par Lunar non vérifié ; aucune action de jeu |
| Mappings et adaptateur applicatif | Non implémentés faute de correspondances vérifiées | Bloqués |
| Configuration | Paramètres C++ validés | Fichiers de profils et UI non implémentés |

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
- Seuls les pairs numériques exacts `127.0.0.1` et `::1`, en connexion directe,
  avec monde actif, fenêtre focalisée et sans menu sont éligibles. Les autres
  adresses de boucle locale sont refusées par choix conservateur.
- Le futur adaptateur devra fournir le pair réseau observé et une génération
  changée à chaque reconnexion, monde, joueur ou session. Un nom `localhost`
  ou une déclaration de l'utilisateur ne constitue pas cette preuve.
- `syntheticLeft` ne déclenche pas de répétition. L'entrée physique devra provenir
  d'une source distincte vérifiée, aujourd'hui absente.
- Les actions retournées sont des intentions à usage unique : l'adaptateur devra
  les revalider et les consommer sur le thread applicatif, sans file persistante.
- Un `EndMining` d'ancien contexte libère uniquement la réservation possédée de
  cet ancien contexte. Il ne doit jamais modifier le nouveau monde. Le modèle
  ne synthétise aucun bouton système.

La restriction locale est une **limitation du projet**, modifiable dans ses sources,
pas une garantie inviolable ni une preuve que le serveur derrière un proxy est local.
Aucune protection n'est désactivée. Aucun mécanisme de furtivité n'est prévu.

## Lire les preuves et les limites

- [Compatibilité observée et blocages](docs/COMPATIBILITY.md)
- [Architecture et contrats d'intégration](docs/ARCHITECTURE.md)
- [Résultats réellement obtenus](docs/TEST_RESULTS.md)
- [Construire et charger le diagnostic Forge](docs/FORGE_DIAGNOSTIC.md)

Le classement d'armure compare protection, score d'attributs fourni par le modèle,
durabilité relative puis absolue, et identifiant stable. Ce n'est pas un calcul
validé des dégâts ou enchantements Minecraft. Le protocole d'inventaire réel,
les identifiants d'objets et la confirmation serveur restent à établir.

La portée contrôle une sélection géométrique locale ; elle n'augmente pas la portée
acceptée par un serveur. Les obstacles sont des boîtes fournies par le modèle,
sans lecture réelle des formes de collision Minecraft.
