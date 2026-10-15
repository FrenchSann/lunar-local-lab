# Adaptateur Minecraft réel : état et utilisation

Le pont C++/JNI charge une classe Java du projet dans le ClassLoader réel de
Minecraft. Il enregistre trois fonctions natives (planification, choix d'armure,
sélection géométrique) et utilise `Minecraft.addScheduledTask` pour effectuer
les lectures et écritures **sur le thread applicatif**.

Les classes `net.minecraft.*` présentes dans cette instance sont nommées. Les
noms/descripteurs ont été observés via JVMTI dans le PID 12836, puis résolus par
réflexion ou JNI. Aucun mapping a397868 ni offset de tas n'est utilisé.

## Démarrer et arrêter

Après compilation avec `LUNAR_LAB_PROCESS_DIAGNOSTIC=ON`, et après avoir identifié
le PID actuel de sa propre instance :

```powershell
.\build\Release\inspect_process.exe PID_ACTUEL --game-control install
.\build\Release\inspect_process.exe PID_ACTUEL --game-control status
.\build\Release\inspect_process.exe PID_ACTUEL --game-control stop
```

Dans la fenêtre du jeu : **F8** arme/désarme ; **F9** arrête l'ordonnanceur.
L'installation est toujours désarmée. Une perte de focus, ouverture de menu,
reconnexion ou changement de monde/joueur désarme les modules. Il faut réarmer
après le retour à un contexte admissible.

L'adaptateur exige un pair réseau de boucle locale, une adresse de connexion
numérique explicite `127.0.0.1` ou `::1`, et un proxy Java de type DIRECT.
La partie solo à canal interne et les noms DNS, même `localhost`, ne sont pas
admis par cette version. Cette restriction est une limitation modifiable du projet.

Le fichier `lunar-local-lab.properties` est créé dans `Minecraft.mcDataDir`.
Pour l'instance inspectée, il se trouve dans `%APPDATA%\.minecraft` :

```properties
repeatMillis=100
armorTimeoutMillis=1500
armorThreshold=0.15
reach=3.0
repeatEnabled=true
armorEnabled=true
selectionEnabled=true
```

La portée doit être dans ]0,6], la période entre 20 et 1000 ms et le délai d'armure
entre 500 et 10000 ms. Après modification, `stop` puis `install` relisent le fichier
si aucune transaction d'équipement n'est restée ouverte. Aucun réglage de portée
ne change la validation du serveur.

## Actions implémentées

- Répétition : état du bouton physique via LWJGL ; cadence C++ à horloge monotone ;
  appel de `Minecraft.clickMouse` sur le thread du jeu. Aucune entrée OS synthétique.
- Minage : lorsque le bouton est tenu et la cible actuelle est un bloc, aucun clic
  répété n'est injecté ; le maintien natif de Minecraft continue. Le composant
  n'usurpe pas l'état du bouton physique.
- Armure : observation des slots réels, classement C++ selon protection, score
  d'attributs/enchantements puis durabilité. L'ordre de remplacement passe par
  `PlayerControllerMP.windowClick`, en trois étapes : prendre le remplacement,
  échanger avec l'armure, ranger l'ancienne pièce. Chaque étape dépend d'une
  réobservation des contenus et du curseur ; aucune écriture directe d'ItemStack.
- Sélection : yeux, direction et boîtes d'entités lus dans le jeu, intersection C++,
  obstruction via `World.rayTraceBlocks`. La cible et le point d'impact sont
  temporairement assignés à `objectMouseOver` et `pointedEntity` pour l'appel de
  clic, puis les références précédentes sont restaurées dans un `finally`.

## Limites de transaction et de classement

La confirmation d'armure est une **confirmation locale par réobservation**, pas
une interception des accusés serveur. Le protocole du jeu reste responsable de
l'acceptation et d'une éventuelle correction. Les délais de réobservation de
100/200 ms ne prouvent pas l'acceptation serveur.

Après expiration, aucune nouvelle opération n'est émise. La réobservation en
état désarmé peut clôturer la transaction si les deux slots et le curseur forment
un état final ou restauré cohérent. Sinon, inspecter le curseur dans l'inventaire.
Un changement de monde/joueur annule les références à l'ancien contexte, sans
tenter de rollback sur le nouveau monde.

Le score d'attributs est une heuristique explicite : niveaux d'enchantement et
montants positifs de modificateurs. Il ne calcule pas les dégâts effectifs selon
le type d'attaque et ne distingue pas encore toutes les opérations d'attributs.
La protection de base reste le premier critère. Les quatre parcours ne sont
pas qualifiés de validés en jeu tant qu'ils n'ont pas été observés effectivement.

## Preuves obtenues pendant cette passe

- Inventaire JVMTI : 1723 classes correspondant au filtre, sans troncature.
- Résolution de `Minecraft`, de son ordonnanceur, des slots, du contrôleur,
  des boîtes de collision et de leurs méthodes dans l'instance réelle.
- Construction du pont et lecture d'état sur le thread Minecraft réussies :
  `world=true`, `loopbackPeer=false`, `focused=false`, `menu=true`.
- Plus de 800 passages sur le thread applicatif sans action dans ce contexte.
- Arrêt à `ticks=830`, compteur inchangé au contrôle suivant.
- Tentative d'armement refusée ; compteurs de clic, remplacement et sélection à zéro.

Les clics réels, la conservation du minage, l'équipement et la sélection en jeu
restent donc **implémentés mais non validés par un essai actif** dans cette passe.

## Durée de vie

Le diagnostic simple peut se décharger. Le pont de jeu, lui, conserve volontairement
sa DLL jusqu'à la sortie de la JVM : ses méthodes natives restent enregistrées.
`stop` désarme et arrête l'ordonnanceur ; les tâches Java déjà en file retournent
sans action. Aucun déchargement n'est tenté tant que ces références peuvent exister.
Fermer l'instance avant de remplacer/recompiler la DLL qu'elle utilise.

Cette version ne constitue pas un mod Forge : la classe Java sert d'adaptateur
au thread applicatif, tandis que les décisions principales restent dans le cœur C++.
