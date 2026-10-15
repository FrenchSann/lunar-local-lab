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
