# TradeTogether v0.6.0

Interface d'echange a offres synchronisees pour Skyrim Together Reborn.

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

## Connexion distante

TradeTogether utilise un canal UDP independant sur le port **27993**. La
decouverte automatique fonctionne sur un reseau local, mais les broadcasts LAN
ne traversent pas Internet.

La version 0.6.0 reprend le principe de MorphSyncTogether : `RemotePeers`,
`RelayMode` et `SharedSecret`.

## Configuration Internet recommandee : un relais

Choisissez la machine qui heberge la session Skyrim Together, ou celle qui a le
plus facilement acces au routeur. Ouvrez/redirigez le port **UDP 27993** vers
ce PC et autorisez Skyrim dans le pare-feu Windows.

Machine relais :

```ini
[Network]
Disabled=0
AutoDiscovery=0
RelayMode=1
LocalPort=27993
RemotePeers=
SharedSecret=remplacez-par-la-meme-valeur-privee-partout
```

Chaque client distant :

```ini
[Network]
Disabled=0
AutoDiscovery=0
RelayMode=0
LocalPort=27993
RemotePeers=ip-publique-ou-dns-du-relais:27993
SharedSecret=remplacez-par-la-meme-valeur-privee-partout
```

Un exemple pret a adapter est fourni dans `TradeTogether_Player2.ini`.

Les clients envoient des paquets de decouverte periodiques au relais. Cela
ouvre leur route NAT sortante, donc ils n'ont normalement pas besoin de
redirection de port. Le relais apprend leur adresse publique observee et
transmet les paquets d'echange aux autres pairs actifs. Les paquets relayes
sont marques pour eviter les boucles.

`SharedSecret` active une signature HMAC-SHA256 sur les paquets de decouverte
et d'echange. La valeur doit etre identique chez tous les joueurs et n'est
jamais transmise sur le reseau.

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

Utiliser l'adresse du serveur Skyrim Together comme `RemotePeers` fonctionne
si ce meme ordinateur execute aussi le relais TradeTogether et expose le port
UDP 27993. Le protocole STR ne relaye pas automatiquement les paquets
TradeTogether : le meme serveur peut etre reutilise comme machine, pas comme
canal reseau interne.

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

Le script compile la DLL, la copie avec l'INI dans le paquet et cree :

```text
dist/TradeTogether-v0.6.0-Vortex.zip
```
