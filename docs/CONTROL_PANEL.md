# Interface Windows C++

`lunar_control.exe` est une interface Win32 native pour Lunar Client / Minecraft 1.8.9.
Elle configure le pont existant ; elle ne propose pas un editeur arbitraire de toute la memoire.
Le pont lit les objets verifies et applique les actions via JNI sur le thread Minecraft.

## Utilisation

1. Extraire toute l'archive dans un dossier. Conserver `lunar_control.exe`,
   `inspect_process.exe` et `lunar_memory_probe.dll` ensemble.
2. Lancer `lunar_control.exe` et saisir le PID Java de sa propre instance Lunar
   (Gestionnaire des taches, onglet Details).
3. Verifier le chemin du fichier `lunar-local-lab.properties` dans le dossier de jeu.
   Le chemin propose est `%APPDATA%\.minecraft\lunar-local-lab.properties`.
4. Charger le fichier ou choisir les valeurs, puis Enregistrer.
5. Cliquer Arreter puis Installer pour relire la configuration ; Installer charge
   le pont dans le processus selectionne. Verifier la reponse avec Etat.
6. Revenir dans un monde solo ou sur un serveur, fermer les menus,
   puis F8 pour armer. F9 arrete les modules.

Portee : ]0,6], cadence : 20..1000 ms, seuil d'armure : 0..1,
delai d'armure : 500..10000 ms. Utiliser un point decimal.
Les cases permettent de configurer repetition, armure et selection separement.
Le minage reste prioritaire selon le contexte du pont.

Enregistrer modifie le fichier, pas instantanement le jeu. Une transaction d'armure
non resolue bloque la relecture ; inspecter le curseur et l'etat avant de poursuivre.
Les messages affiches viennent de l'inspecteur, avec son code de sortie.
Les commandes tournent hors du thread graphique, sans console visible.
La fermeture est suspendue pendant une commande pour laisser celle-ci se terminer.
Fermer l'interface ne stoppe pas le pont : utiliser Arreter ou F9.
La DLL reste chargee jusqu'a la fermeture de Minecraft.

Si un pont est deja charge depuis un autre dossier, l'inspecteur refuse la nouvelle
copie. Fermer puis relancer Minecraft avant d'utiliser cette archive ; ne pas tenter
un dechargement force. Pour une instance deja liee au dossier de compilation,
utiliser le `lunar_control.exe` de ce meme dossier.

La portee concerne la selection locale avec obstacles. Elle ne change pas la
validation serveur. Depuis 0.3.1, aucune restriction d'adresse reseau n'est imposee.
Les quatre actions restent a valider dans un contexte de jeu admissible.

## Compilation et verification

Depuis un environnement MSVC x64, CMake, PowerShell 7 et JDK 9+ :

```powershell
cmake --preset windows-msvc -DLUNAR_LAB_JNI_PROBE=ON -DLUNAR_LAB_PROCESS_DIAGNOSTIC=ON
cmake --build --preset release
ctest --preset release --output-on-failure
```

L'interface seule se compile sous Windows sans JNI : cible `lunar_control`.
Les commandes d'integration necessitent toutefois les deux fichiers compagnons.
`lunar_control.exe --self-test` verifie les valeurs limites et rejette les nombres
invalides, non finis, avec suffixe et les periodes fractionnaires.
Les tests ne constituent pas une validation visuelle ni un essai des actions en jeu.
