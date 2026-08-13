# TradeTogether v0.3.1

Version simplifiee basee sur le comportement valide en jeu avec `openactorcontainer 1` sous Skyrim Together Reborn.

## Fonctionnement

1. Connectez les deux joueurs a Skyrim Together Reborn.
2. Visez l'autre personnage avec le reticule.
3. Appuyez sur **F6**.
4. TradeTogether appelle la fonction native Papyrus `Actor.OpenInventory(true)` sur l'acteur cible via la VM de Skyrim.
5. Utilisez l'interface d'inventaire native de Skyrim pour donner/prendre les objets.
6. Skyrim Together Reborn reste responsable de la synchronisation d'inventaire et des donnees d'instance.

## Important

Cette v0.3 n'essaie pas encore d'identifier formellement un "joueur STR". Elle accepte tout Actor autre que le joueur local. C'est volontaire : le transfert via l'inventaire de l'acteur a ete valide en jeu, alors que STR ne fournit pas d'API SKSE publique stable permettant a un plugin tiers de classifier proprement ses acteurs distants.

Le log `Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log` enregistre le FormID, le nom et le Base FormID de la cible. Ces informations permettront d'ajouter un filtre fiable apres un test sur Elir.

## Build

Depuis PowerShell dans le dossier du projet :

```powershell
.\build_release.bat
```

Le script :
- synchronise automatiquement la `builtin-baseline` avec `C:\dev\vcpkg` ;
- configure CMake ;
- compile en Release ;
- copie `TradeTogether.dll` dans `package/Data/SKSE/Plugins` ;
- cree automatiquement `dist/TradeTogether-v0.3.1-Vortex.zip`.

## Archive Vortex sans recompiler

```powershell
.\make_vortex_archive.bat
```
