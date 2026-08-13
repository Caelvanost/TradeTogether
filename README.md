# TradeTogether v0.5.0

Interface d’échange à offres synchronisées pour Skyrim Together Reborn.

## Déroulement d’un échange

1. Les deux joueurs installent la même version de TradeTogether et se
   connectent à Skyrim Together Reborn.
2. Le demandeur vise l’autre personnage et appuie sur **F6**.
3. Le joueur ciblé reçoit le prompt **Accepter / Refuser**.
4. Après acceptation, l’inventaire personnel s’ouvre chez les deux joueurs en
   mode composition d’offre.
5. Chaque joueur sélectionne ses objets :

   - **E** : ajouter une unité de l’objet sélectionné ;
   - **Suppr** : retirer une unité ;
   - **F6** : afficher les deux offres et se déclarer prêt ;
   - **Tab** : annuler tout l’échange.

   TradeTogether n’utilise pas F8, F9 ou F10 afin de ne pas entrer en conflit
   avec Heart of Magic, le chargement rapide et OStim Together.

6. Toute modification retire automatiquement l’état « prêt ».
7. Lorsque les deux joueurs sont prêts, chacun voit les deux paniers et doit
   confirmer une dernière fois.
8. Après la double confirmation, l’inventaire natif de la cible s’ouvre chez le
   demandeur pour réaliser le transfert validé.

Les objets de quête sont refusés. Une offre accepte jusqu’à 24 lignes et peut
être vide afin d’autoriser un don à sens unique. La demande initiale expire
après 30 secondes et une session inactive après 5 minutes.

## Limite actuelle

L’interface synchronise et verrouille l’intention des deux joueurs, mais elle
ne déplace pas automatiquement les objets. Skyrim Together Reborn n’expose pas
d’API publique stable garantissant le transfert des instances enchantées,
améliorées ou renommées. Le transfert final reste donc manuel dans l’interface
d’inventaire native déjà validée avec STR.

## Réseau

TradeTogether utilise un canal UDP indépendant sur le port **27993**. La
découverte est automatique sur un réseau local. Les deux joueurs doivent
autoriser Skyrim dans leur pare-feu privé.

Pour une connexion sans découverte LAN, définissez `AutoDiscovery=0`,
`PeerHost` et `PeerPort` dans
`Data/SKSE/Plugins/TradeTogether.ini`. Une redirection UDP peut être nécessaire
selon le routeur.

Les joueurs sont associés par leur nom de personnage. Comme les versions
précédentes, la cible initiale peut être tout `Actor` autre que le joueur local,
car STR ne fournit pas d’API SKSE publique stable permettant de classifier ses
acteurs distants.

Le journal se trouve dans
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log`.

## Build

```powershell
.\build_release.bat
```

Le script compile la DLL, la copie avec l’INI dans le paquet et crée :

```text
dist/TradeTogether-v0.5.0-Vortex.zip
```
