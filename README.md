# TradeTogether v0.4.0

Interface d’échange avec consentement distant pour Skyrim Together Reborn.

## Fonctionnement

1. Installez TradeTogether chez les deux joueurs et connectez-vous à Skyrim
   Together Reborn.
2. Visez l’autre personnage avec le réticule.
3. Appuyez sur **F6**.
4. TradeTogether envoie une demande au propriétaire du personnage ciblé.
5. Le joueur ciblé reçoit une boîte de dialogue native Skyrim :
   **Accepter** ou **Refuser**.
6. L’inventaire ciblé ne s’ouvre chez le demandeur qu’après réception de
   l’acceptation.

Une demande expire après 30 secondes. En cas de transport réseau indisponible,
de refus, de délai dépassé ou de changement de cible, l’inventaire reste fermé.

## Réseau

TradeTogether utilise un petit canal UDP indépendant, car Skyrim Together
Reborn ne fournit pas encore d’API publique permettant à un plugin SKSE tiers
d’envoyer ce type de demande. La découverte est automatique sur le réseau local
via le port UDP **27993**.

Les deux joueurs doivent autoriser Skyrim dans le pare-feu privé et utiliser la
même version de TradeTogether. Pour une connexion qui ne partage pas le même
réseau local, désactivez `AutoDiscovery` et renseignez l’adresse IPv4 joignable
dans `Data/SKSE/Plugins/TradeTogether.ini`; une redirection UDP peut être
nécessaire selon le routeur.

## Inventaire et synchronisation

Après l’accord distant, TradeTogether appelle la fonction native Papyrus
`Actor.OpenInventory(true)` sur le proxy local de l’acteur ciblé. Skyrim
Together Reborn reste responsable de la synchronisation des objets et des
données d’instance.

Cette version identifie les propriétaires par le nom de personnage annoncé sur
le canal TradeTogether. Comme la v0.3, elle accepte comme cible tout `Actor`
autre que le joueur local : STR ne fournit pas d’API SKSE publique stable pour
classifier formellement ses acteurs distants.

Le journal
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log` enregistre
la découverte des pairs, les demandes, les réponses et l’ouverture finale.

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
- crée `dist/TradeTogether-v0.4.0-Vortex.zip` avec la DLL et l’INI.

## Archive Vortex sans recompiler

```powershell
.\make_vortex_archive.bat
```
