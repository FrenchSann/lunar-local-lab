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
