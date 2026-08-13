# TradeTogether v0.8.1-strpm5 branch

Interface d'echange a offres synchronisees pour Skyrim Together Reborn.

## Installation Vortex

L'archive Vortex utilise un FOMOD avec deux profils. Sur cette branche, les
deux profils utilisent `Transport=STRPlugin`, donc TradeTogether ne demande
plus d'ouverture de port quand `STRPluginMessagingAPI.dll` fournit un vrai
pont via la connexion Skyrim Together Reborn.

- **Client / Local** : choix recommande pour les clients distants, le LAN, ou
  une installation sans relais Internet.
- **Host** : profil reserve a la machine hote si on doit garder une difference
  de role plus tard. En mode STRPlugin pur, il n'a plus besoin de redirection
  de port.

Pour jouer a distance sans ouverture de port, installez aussi
`STRPluginMessagingAPI.dll` et choisissez **Client / Local** sur les clients.
Le canal utilise est `chaos.trade_together.offer.v1`.

Si le plugin STRPM n'est pas installe, `Transport=STRPlugin` refuse de demarrer
le reseau TradeTogether au lieu de retomber silencieusement sur l'UDP. Si STRPM
est installe mais que son bridge STR n'est pas encore actif, l'interface peut
s'enregistrer et les envois retourneront `not connected` avec un statut
diagnostic dans `TradeTogether.log`. Pour tester l'ancien comportement, mettez
`Transport=Auto` ou `Transport=UDP`.

## Deroulement d'un echange

1. Les deux joueurs installent la meme version de TradeTogether et se
   connectent a Skyrim Together Reborn.
2. Le demandeur vise l'autre personnage et appuie sur **F6**.
3. Le joueur cible recoit le prompt **Accepter / Refuser**.
4. Apres acceptation, l'inventaire personnel s'ouvre chez les deux joueurs en
   mode composition d'offre.
5. Chaque joueur selectionne ses objets :

   - **E** : ajouter une unite de l'objet selectionne ;
   - **Suppr** : retirer une unite ;
   - **F6** : afficher les deux offres et se declarer pret ;
   - **Tab** : annuler tout l'echange.

   TradeTogether n'utilise pas F8, F9 ou F10 afin de ne pas entrer en conflit
   avec Heart of Magic, le chargement rapide et OStim Together.

6. Toute modification retire automatiquement l'etat "pret".
7. Lorsque les deux joueurs sont prets, chacun voit les deux paniers et doit
   confirmer une derniere fois.
8. Apres la double confirmation, l'inventaire natif de la cible s'ouvre chez le
   demandeur pour realiser le transfert valide.

Les objets de quete sont refuses. Une offre accepte jusqu'a 24 lignes et peut
etre vide afin d'autoriser un don a sens unique. La demande initiale expire
apres 30 secondes et une session inactive apres 5 minutes.

## Transport STR Plugin Messaging

Cette branche suit le nouveau STRPM v5 du workspace. TradeTogether :

- charge `STRPluginMessagingAPI.dll` ;
- enregistre le canal `chaos.trade_together.offer.v1` ;
- envoie les payloads d'echange en reliable/ordered ;
- garde le format actuel `TTNET|v1|...` pour limiter le risque de regression ;
- lit les diagnostics STRPM v2 (`RuntimeStatus`) ;
- refuse un STRPM qui serait actif sur son backend UDP de developpement ;
- ne cree aucun socket UDP TradeTogether quand `Transport=STRPlugin`.

Le nouveau runtime STRPM est pense en deux couches :

- `STR_QueryPluginMessagingInterface` pour les mods SKSE comme TradeTogether ;
- `STRPM_QueryTransportInterface` pour le bridge prive cote STR.

Le build STRPM package par defaut n'ouvre pas d'UDP. Si le bridge
`Data\SkyrimTogetherReborn\STRPluginMessagingBridge.dll` manque ou ne demarre
pas encore, STRPM garde le backend actif a `None` et les envois peuvent
retourner `not connected`.

## Connexion distante automatique avec STR via UDP legacy

Le transport UDP historique reste disponible avec `Transport=UDP` ou
`Transport=Auto`. Dans ce mode, TradeTogether utilise son propre canal UDP sur
le port **27993**. Il
ne parle pas dans le port STR et ne modifie pas le protocole STR.

La version 0.7.1 reprenait la methode de MorphSyncTogether : quand un client STR
s'est connecte en direct a un serveur, STR garde la derniere adresse utilisee
dans son stockage Chromium :

```text
Data\SkyrimTogetherReborn\cache\Default\Local Storage\leveldb
```

TradeTogether lit cette adresse, retire le port STR si elle en contient un,
puis contacte le meme hote sur `AutoRemotePort`, par defaut **27993**.

Exemple :

```text
STR direct connect : 82.65.51.103:10578
TradeTogether      : 82.65.51.103:27993
```

Configuration client par defaut :

```ini
[Network]
Disabled=0
AutoDiscovery=1
RelayMode=0
LocalPort=27993
AutoRemoteFromSTR=1
AutoRemotePort=27993
AutoSharedSecretFromSTR=0
RemotePeers=
SharedSecret=
```

En pratique, le client distant se connecte d'abord au serveur STR, puis
TradeTogether recupere automatiquement l'hote STR au lancement ou pendant les
essais periodiques suivants. Si STR n'a encore rien enregistre, `RemotePeers`
reste disponible comme solution manuelle.

## Configuration Internet legacy UDP : un relais

Choisissez la machine qui heberge la session Skyrim Together, ou celle qui a le
plus facilement acces au routeur. Ouvrez/redirigez le port **UDP 27993** vers
ce PC et autorisez Skyrim dans le pare-feu Windows.

Machine relais :

```ini
[Network]
Transport=UDP
Disabled=0
AutoDiscovery=0
RelayMode=1
LocalPort=27993
AutoRemoteFromSTR=0
AutoRemotePort=27993
AutoSharedSecretFromSTR=0
RemotePeers=
SharedSecret=remplacez-par-la-meme-valeur-privee-partout
```

Le FOMOD legacy installait automatiquement ce profil si vous choisissiez
**Host**. Le fichier `TradeTogether_RelayHost.ini` reste fourni dans le depot
comme modele manuel si vous preferez utiliser l'override separe.

Chaque client distant peut rester en configuration automatique :

```ini
[Network]
Transport=UDP
Disabled=0
AutoDiscovery=1
RelayMode=0
LocalPort=27993
AutoRemoteFromSTR=1
AutoRemotePort=27993
RemotePeers=
SharedSecret=remplacez-par-la-meme-valeur-privee-partout
```

Les clients envoient des paquets de decouverte periodiques au relais. Cela
ouvre leur route NAT sortante, donc ils n'ont normalement pas besoin de
redirection de port. Le relais apprend leur adresse publique observee et
transmet les paquets d'echange aux autres pairs actifs. Les paquets relayes
sont marques pour eviter les boucles.

## Secret partage

`SharedSecret` active une signature HMAC-SHA256 sur les paquets de decouverte
et d'echange. La valeur doit etre identique chez tous les joueurs et n'est
jamais transmise sur le reseau.

Option avancee, comme MorphSyncTogether : si `AutoSharedSecretFromSTR=1` et
que `SharedSecret` est vide, TradeTogether tente de reutiliser le mot de passe
STR :

- en mode relais, il lit `sPassword` dans
  `Data\SkyrimTogetherReborn\config\STServer.ini` ;
- en mode client, il lit le mot de passe direct-connect sauvegarde par STR.

Si aucun mot de passe STR n'est trouve, utilisez `SharedSecret` manuellement.

## Pairs Internet directs

Sans relais, laissez `RelayMode=0` et listez tous les autres endpoints publics.
Chaque joueur liste doit ouvrir son propre port UDP :

```ini
RemotePeers=joueur-2.example:27993,203.0.113.8:27994
```

`RemotePeers` accepte les IPv4 et les noms DNS, separes par virgule ou
point-virgule. L'ancien couple `PeerHost` / `PeerPort` reste supporte et est
ajoute a `RemotePeers`.

## A propos du serveur STR

On peut reutiliser l'adresse de la machine STR, mais pas le port STR comme
canal TradeTogether. Le serveur STR peut heberger le relais TradeTogether si le
port UDP 27993 est expose, mais STR ne relaye pas automatiquement les paquets
TradeTogether.

## Limite actuelle

L'interface synchronise et verrouille l'intention des deux joueurs, mais elle
ne deplace pas automatiquement les objets. Skyrim Together Reborn n'expose pas
d'API publique stable garantissant le transfert des instances enchantees,
ameliorees ou renommees. Le transfert final reste donc manuel dans l'interface
d'inventaire native deja validee avec STR.

Les joueurs sont associes par leur nom de personnage. Comme les versions
precedentes, la cible initiale peut etre tout `Actor` autre que le joueur local,
car STR ne fournit pas d'API SKSE publique stable permettant de classifier ses
acteurs distants.

Le journal se trouve dans
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log`.

## Build

```powershell
.\build_release.bat
```

Le script compile la DLL, prepare les profils FOMOD Host / Client Local et cree :

```text
dist/TradeTogether-v0.8.1-strpm5-Vortex.zip
```
