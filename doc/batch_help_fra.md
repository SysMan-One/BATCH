<!-- ident: BATCH -->

# BATCH -- soumettre et gérer des travaux par lots

Exécute un script d'interpréteur de commandes en tant que travail par
lots : le script est placé dans une file d'attente et exécuté plus
tard, sans terminal, sous la conduite d'un contrôleur de travaux
résident.

Format :

```
BATCH commande [paramètre] [/qualificatifs]
```

Soumettre un script n'est pas l'exécuter. La soumission attribue un
numéro d'entrée dans la file et rend la main ; le processus est créé
plus tard, si et quand le contrôleur en décide ainsi. C'est à
l'entrée, non au processus, que l'on se tient.

Un travail qui échoue demeure dans la file avec son journal, afin
qu'on puisse l'examiner. Un travail qui réussit quitte la file. C'est
ce qui rend utile SHOW sans qualificatif : ce qui reste est ce qui
s'exécute et ce qui a mal tourné.

Une commande s'abrège à tout début non ambigu de son nom, comme un
qualificatif : SUBM vaut SUBMIT et DEL vaut DELETE, tandis que S est
refusé, SHOW et SUBMIT commençant tous deux par cette lettre.

Le répertoire racine du sous-système est pris dans la variable
d'environnement BATCH_ROOT, et à défaut dans la valeur fixée à la
construction.

# SUBMIT -- mettre un script en file d'attente

Place un script d'interpréteur en file d'attente pour exécution en
travail par lots.

Format :

```
BATCH SUBMIT spécification-de-script [/qualificatifs]
```

La spécification est résolue en chemin absolu au moment où la
commande est entrée. Le fichier doit exister et être lisible par vous
comme par le contrôleur de travaux.

Le travail démarre dans le répertoire de travail courant lors de la
soumission, non dans celui qui sera courant au moment de l'exécution.

Une spécification de fichier et un qualificatif commencent tous deux
par une barre oblique ; un argument n'est donc tenu pour un
qualificatif que si le texte jusqu'au signe égal ne porte pas de
barre à lui : /tmp/job.sh est un chemin, /NAME=x un qualificatif.

## /AFTER

Indique l'heure avant laquelle le travail ne doit pas démarrer. Le
travail est retenu jusque-là et libéré sans que nul s'en occupe.

```
/AFTER=+1-02:00           dans un jour et deux heures
/AFTER=29-JUL-2026:03:00  un moment précis, à votre horloge
/AFTER=TOMORROW           le minuit par lequel commence le jour suivant
```

La forme relative est +[jours-]hh[:mm[:ss]], les champs omis valant
zéro. Le champ des jours se distingue de celui des heures par le
tiret qui le suit, et par rien d'autre :

```
+7          sept heures
+7-         sept jours
+7-12:00    sept jours et douze heures
+:30        trente minutes
+::45       quarante-cinq secondes
```

L'erreur ne se voit qu'une fois le délai écoulé ; aussi la soumission
nomme-t-elle l'heure à laquelle le travail démarrera :

```
$ batch submit nightly.sh /AFTER=+7-
%BATCH-S-QUEUEDUNTIL, job nightly queued on SYS$BATCH as entry 4, holding until  9-AUG-2026 20:03:37
```

## /CPU

Lie le travail aux processeurs nommés, dans la notation de
taskset(1) : nombres et intervalles séparés par des virgules.

```
/CPU=0-3,5,8-11
```

Sans ce qualificatif, le travail hérite du masque d'affinité du
contrôleur, ou reçoit le processeur suivant du pool si le contrôleur
a été lancé avec /LCORE. La liste est vérifiée à la saisie.

## /HOLD

Place le travail dans l'état Holding. Il n'est pas considéré pour
exécution tant que la commande RELEASE ne l'a pas libéré.

## /KEEP

Conserve le journal du travail après une fin heureuse. Par défaut, le
journal d'un travail réussi disparaît avec son entrée.

Un journal nommé par /LOG_FILE n'est jamais supprimé : c'est votre
fichier, et vous en disposez.

## /LOG_FILE

Nomme le fichier où sont dirigées la sortie standard et la sortie
d'erreur du travail. La spécification est résolue en chemin absolu à
la saisie.

Un tel fichier est ouvert une fois l'identité du travail prise ; ce
sont donc les droits de celui sous le nom duquel il s'exécute qui
décident, non ceux du contrôleur.

Par défaut le journal est créé dans le répertoire d'où le travail a
été soumis, comme il l'est sous OpenVMS, et nommé d'après le travail
et son numéro d'entrée. Si l'on ne peut y écrire, le journal va dans
le sous-répertoire log de la racine du sous-système.

## /NAME

Nomme le travail, jusqu'à 63 caractères. Par défaut le nom du travail
est celui du fichier de script, privé du suffixe .sh.

Un nom doit être un nom : ni barre oblique, ni renvoi au répertoire
parent. Le nom entre dans la spécification du journal, et un tel nom
la conduirait hors du répertoire prévu.

## /PARAMETERS

Transmet au travail jusqu'à huit valeurs.

```
/PARAMETERS=(full,/srv/data,verbose)
```

Chaque valeur parvient au script deux fois : comme variables
d'environnement P1 à P8 et comme arguments positionnels $1 à $8. Les
valeurs sont remises à l'interpréteur en éléments distincts du
vecteur d'arguments et jamais réunies en ligne de commande ; une
valeur portant des caractères que l'interpréteur interpréterait ne
peut donc rien faire exécuter.

Les parenthèses sont lues par l'interpréteur qui appelle BATCH ;
mettez le qualificatif entier entre apostrophes, ou passez-vous
d'elles.

## /PRIORITY

Fixe la priorité du travail dans la file, de 1 à 255, 100 par défaut.
Le contrôleur choisit le travail en attente de plus haute priorité ;
à priorité égale, l'ordre des numéros d'entrée décide.

C'est la priorité dans la file. Elle n'a rien à voir avec la priorité
d'ordonnancement du processus et ne signifie plus rien une fois le
travail lancé.

## /QUEUE

Nomme la file où le travail est placé. Par défaut celle avec laquelle
le contrôleur a été lancé.

## /RESTART

Indique que le travail doit être exécuté de nouveau si le contrôleur
a été arrêté pendant son exécution. Un tel travail retourne dans la
file au prochain démarrage ; sans ce qualificatif il est conservé
comme orphelin et n'est pas relancé.

Le processus laissé par l'instance précédente n'est ni recherché ni
arrêté : il appartient à une session à lui et peut fort bien avoir
fini. Le qualificatif promet que le travail sera exécuté, non qu'il
le sera une seule fois ; écrivez-le donc de sorte qu'une double
exécution ne nuise pas.

Le travail apprend le redémarrage par BATCH_RESTART, qui vaut TRUE ou
FALSE, et par BATCH_RESTARTS, qui les compte.

## /USER

Exécute le travail sous le nom de l'utilisateur nommé, et non sous
celui qui l'a soumis.

```
/USER=sysman
```

Seul l'utilisateur privilégié peut le demander. L'identité est
établie par le contrôleur d'après ce que le noyau rapporte de
l'expéditeur, non d'après ce que dit la requête : l'utilitaire
s'exécute sous le nom de qui l'a lancé et pourrait se dire n'importe
qui.

Tout usage de ce qualificatif et tout refus sont consignés dans le
journal du contrôleur, avec le nom du demandeur et celui du
bénéficiaire :

```
%BATCH-S-RUNASUSER, job 1 submitted by uid 0 to run as sysman (uid 1000, gid 1000)
%BATCH-E-NOPRIVUSER, uid 1000 may not submit a job to run as root
```

Le travail démarre dans le répertoire personnel de cet utilisateur et
non là d'où il a été soumis, où il pourrait n'être pas admis ; si ce
répertoire n'existe pas, la racine sert. Le journal va dans le
sous-répertoire log du sous-système : ce qui vaut pour les droits
d'un utilisateur ne dit rien de ceux d'un autre.

## /RETAIN

Fixe la condition sous laquelle l'entrée survit au travail.

```
/RETAIN=ALWAYS      l'entrée est toujours conservée
/RETAIN=ERROR       seulement en cas d'échec ; c'est le défaut
/RETAIN=NEVER       jamais conservée, même en cas d'échec
```

# SHOW -- afficher la file d'attente

Affiche les entrées de la file des travaux par lots.

Format :

```
BATCH SHOW [numéro-d-entrée] [/qualificatifs]
```

Un numéro d'entrée n'affiche que celle-là, quel que soit son état ;
SHOW d'une entrée ne dépend donc pas de /ALL comme en dépend SHOW de
la file. Une entrée inexistante est refusée plutôt qu'affichée comme
une file vide. Le numéro peut aussi être donné par /ENTRY.

Sans /ALL sont affichés les travaux en cours, ceux qu'on interrompt,
ceux qui attendent (Pending et Holding) et ceux conservés après échec :
la file telle qu'elle reste à traiter. L'entrée d'un travail réussi
quitte la file ; celle que /RETAIN=ALWAYS conserve n'apparaît qu'avec
/ALL.

La colonne Elapsed est le temps écoulé du début du travail à sa fin,
ou à l'instant présent tant qu'il s'exécute.

La dernière colonne dit ce qu'il est advenu du travail : exit=n pour
celui qui a rendu ce code, signal=n pour celui qu'un signal a
terminé. Pour un travail retenu jusqu'à une heure, elle porte cette
heure : un travail qui n'a pas tourné n'a pas de fin à rapporter.
Connaître l'heure de départ ne demande donc pas /FULL.

L'utilisateur privilégié n'est pas filtré par la propriété et n'a que
faire de /EVERYONE : il peut supprimer et libérer toute entrée, et
une entrée dont il dispose sans pouvoir la voir serait pire
qu'inutile.

## /ALL

Ajoute à l'affichage les entrées conservées après un travail réussi
(/RETAIN=ALWAYS).

## /EVERYONE

Affiche les travaux de tous les utilisateurs. Par défaut les vôtres
seulement.

## /FULL

Affiche chaque entrée en entier : le propriétaire, la priorité, les
processeurs, le script, le répertoire de travail, le journal, les
horodatages, le code de fin et les paramètres.

## /MANAGER

Affiche l'état du contrôleur de travaux plutôt que le contenu de la
file : l'image qu'il exécute, son processus et depuis quand il est
levé, la racine qu'il dessert, la limite de travaux et ce qui s'y
exécute, le compte par état, le prochain numéro d'entrée et le pool
de processeurs qui lui a été donné.

Une file immobile a la même apparence, que le contrôleur soit occupé,
oisif ou absent -- et c'est la première chose à savoir quand rien ne
se passe.

## /QUEUES

Affiche les files plutôt que les travaux : chacune avec sa limite, ce
qu'elle a en cours, en attente et retenu, et si elle est suspendue.

## /PAGE

Retient l'affichage au bas de chaque écran et attend une touche, pour
une file plus longue que le terminal.

Le qualificatif est une courtoisie envers qui lit à un terminal et
une gêne pour un programme qui lit un tube ; il n'est donc accordé
que là où il y a de quoi retenir : redirigé, il est ignoré sans mot
dire et la sortie est exactement celle qu'elle eût été sans lui.

## /QUEUE

Restreint l'affichage à la file nommée.

# DELETE -- retirer une entrée de la file

Retire des entrées de la file, en terminant les travaux en cours.

Format :

```
BATCH DELETE liste-d-entrées
```

La liste s'écrit comme DCL les écrit : nombres et intervalles séparés
par des virgules, les parenthèses facultatives -- `1,3,4` aussi bien
que `(1,3-5)`. Chaque élément est appliqué quel qu'ait été le sort
des précédents, et la commande se termine sur un échec si l'un d'eux
a échoué.

Si le travail s'exécute, le signal est envoyé à tout son groupe de
processus et le travail passe dans l'état Aborting ; l'entrée est
traitée à la fin du processus et le journal conservé. Dans tout autre
état l'entrée est retirée aussitôt, et son journal avec elle, sauf si
/KEEP a été donné à la soumission.

Seuls vos propres travaux peuvent être supprimés ; l'utilisateur
privilégié peut supprimer n'importe lequel.

# RELEASE -- libérer un travail retenu

Libère un travail de l'état Holding.

Format :

```
BATCH RELEASE liste-d-entrées
```

Une liste est prise ici comme elle l'est par DELETE.

Le travail passe dans l'état Pending et devient prêt à s'exécuter.
Toute heure /AFTER qu'il portait est effacée, de sorte qu'un travail
libéré n'est pas retenu une seconde fois.

La commande est refusée si le travail n'est pas dans l'état Holding.

# SET -- modifier les attributs d'une entrée de la file

Modifie sur place les attributs d'un travail qui est dans la file.
L'entrée garde son numéro et sa place dans l'ordre ; rien n'est retiré,
rien n'est soumis de nouveau.

Format :

```
BATCH SET ENTRY liste-d-entrées [/qualificatifs]
```

## ENTRY

Modifie les attributs des entrées nommées. Une liste est prise comme
elle l'est par DELETE. Seuls les attributs qu'un qualificatif nomme
sont touchés ; les autres restent ce qu'ils étaient.

```
/PRIORITY=n         la priorité dans la file, de 1 à 255
/AFTER=heure        ne pas démarrer avant l'heure ; /NOAFTER l'efface
/HOLD               retenir le travail ; /NOHOLD libère un travail retenu
/NAME=nom           le nom du travail
/RETAIN=mot-clé     ALWAYS, ERROR ou NEVER, comme pour SUBMIT
/KEEP, /NOKEEP      garder ou non le journal d'un travail réussi
/RESTART, /NORESTART  relancer ou non le travail après un redémarrage
/CPU=liste          les processeurs auxquels le travail est lié
```

Ce qui peut être modifié dépend de l'état du travail. Un travail qui
attend, Pending ou Holding, peut tout voir modifié. Un travail qui
s'exécute ne peut voir modifiés que /RETAIN, /KEEP et /RESTART : le
reste décrit un départ déjà survenu. Un travail retenu après sa fin
(Retained) ne peut rien voir modifié, rien n'étant plus à décider à
son sujet. Une demande qui nomme un attribut que l'état interdit est
refusée en entier.

/HOLD et /AFTER agissent ensemble comme à la soumission. Un travail
retenu qui porte une heure est libéré quand l'heure vient, ou quand
/NOAFTER l'efface ; un travail retenu par /HOLD seul attend RELEASE
ou /NOHOLD. /HOLD avec /NOAFTER signifie donc : retenu, sans heure.

```
$ batch set entry 17 /PRIORITY=200
%BATCH-S-ENTMODIFIED, entry 17 modified: priority=200
$ batch set entry 17 /AFTER=TOMORROW /NAME=nightly2
%BATCH-S-ENTMODIFIED, entry 17 modified: after= 9-SEP-2026 00:00:00, name=nightly2
```

Le nom donné au travail change ce que montrent les affichages et rien
d'autre : le fichier journal a été nommé à la soumission et garde son
nom.

# ANALYZE -- examiner le journal de la file

Examine le journal de la file et rapporte ce qui ne va pas.

Format :

```
BATCH ANALYZE [spécification-de-journal] [/DUMP]
```

Sans spécification, c'est le journal de ce sous-système qui est
examiné.

Le fichier est lu directement, non par l'entremise du contrôleur ; la
commande sert donc là où le contrôleur ne démarre pas -- c'est-à-dire
quand on en a le plus besoin -- et sert de même sur la copie d'un
journal venue d'ailleurs.

Rien n'est écrit. Un journal trouvé endommagé est laissé tel quel :
qu'en faire est l'affaire de qui a lancé la commande, et une
réparation non demandée serait la pire des surprises.

Une étiquette que cette révision n'attribue pas est nommée, avec sa
valeur en hexadécimal :

```
  entry 1: tag 4000, type 0, 6 octets not known to this revision: DE AD BE EF CA FE
```

Ce n'est pas compté pour un défaut -- passer outre ce qu'on ne
connaît pas est précisément ce qui permet au journal de franchir un
changement de révision -- mais il vaut de le dire : un journal écrit
par une révision plus récente et un journal silencieusement corrompu
se ressemblent tant que les étiquettes ne sont pas nommées.

## /DUMP

Affiche aussi les enregistrements : entrée, nom, taille, état,
propriétaire, priorité, heure de mise en file et script exécuté.

# SYNC -- attendre la fin d'un travail

Attend la fin d'un travail et rapporte comment il s'est terminé.

Format :

```
BATCH SYNC numéro-d-entrée [/TIMEOUT=secondes]
```

Destinée au script qui a de l'ouvrage après un travail et nul autre
moyen de savoir quand celui-ci est fait. Le code de retour porte la
réponse ; l'appelant n'a donc rien à lire :

```
0    le travail a réussi
1    le travail a échoué, le délai a expiré, ou l'entrée n'existe pas
```

```sh
batch submit nightly.sh /NAME=nightly
batch sync 17 /TIMEOUT=3600 || echo "le travail nocturne s'est mal passé"
```

Une entrée qui a quitté la file est tenue pour réussie : c'est ainsi
qu'on quitte la file, un travail qui échoue étant conservé. Un numéro
jamais attribué se distingue d'un numéro attribué et depuis parti par
le compteur qui les délivre, et il est refusé.

## /TIMEOUT

Abandonne après le nombre de secondes indiqué, au lieu d'attendre
aussi longtemps qu'il faudra. Sans lui l'attente n'a pas de terme.

# QUEUE -- suspendre une file ou la laisser repartir

Retient le travail d'une file, ou le laisse aller de nouveau.

Format :

```
BATCH QUEUE nom-de-file /PAUSE
BATCH QUEUE nom-de-file /RESUME
```

Une file suspendue ne démarre rien. Ses entrées ne sont pas dérangées
et ses travaux en cours vont jusqu'au bout : suspendre est ce que
l'on fait d'une file dont les travaux doivent attendre sans que leurs
entrées soient touchées.

Seul l'utilisateur privilégié peut le demander : suspendre retient le
travail de tous ceux qui se servent de la file, et la propriété d'un
travail ne dit rien de la file où il attend.

# PURGE -- retirer les entrées des travaux finis

Retire les entrées laissées par les travaux qui ont fini.

Format :

```
BATCH PURGE [/QUEUE=file] [/CONFIRM]
```

Une file qui garde ses échecs a besoin d'être rangée : une entrée
conservée après un échec demeure jusqu'à ce qu'on l'ait regardée --
et on l'a regardée, mais les entrées restent.

Seul un travail qui a fini est pris. Un travail qui s'exécute n'est
pas touché : PURGE range ce qui est fini, il n'arrête pas ce qui ne
l'est pas.

## /QUEUE

Restreint le rangement à la file nommée.

## /CONFIRM

Demande avant d'agir. Tout ce qui n'est pas un mot commençant par Y
est un refus : ce que fait PURGE ne se défait pas, et le silence
n'est pas un consentement. Une entrée redirigée est tenue pour
confirmée, et /NOCONFIRM le dit explicitement.

# HELP -- afficher ces renseignements

Affiche des renseignements sur une commande de ce sous-système.

Format :

```
BATCH HELP [sujet [sous-sujet ...]]
```

Sans sujet, tout le texte est affiché. Un sous-sujet se nomme après
le sujet auquel il appartient :

```
$ batch help submit
$ batch help submit /after
$ batch help show /full
```

# STATES -- les états par lesquels passe un travail

Un travail se trouve dans exactement un des états suivants.

```
Pending     peut s'exécuter et attend une place
Holding     retenu par /HOLD, ou par une heure /AFTER non atteinte
Executing   un processus lui a été créé et s'exécute
Aborting    un signal de fin a été envoyé et il n'a pas fini
Retained    il a fini et son entrée a été conservée pour examen
```

Un travail qui finit avec un code nul et sans /RETAIN=ALWAYS quitte
la file ; tout autre est conservé.

# FILES -- ce que le sous-système garde, et où

```
<racine>/batch.sock     le socket où le contrôleur reçoit les requêtes
<racine>/journal.dat    le journal de la file
<racine>/batchd.log     le journal du contrôleur lui-même
<racine>/log/           les journaux des travaux sans autre place
```

La racine est /var/spool/batch sauf construction contraire ;
l'utilitaire la prend dans la variable d'environnement BATCH_ROOT.

Le journal de la file est écrit dans un fichier temporaire puis
renommé par-dessus la copie précédente : on ne le voit donc jamais à
demi écrit. Son enregistrement se décrit lui-même -- une suite de
valeurs étiquetées, non l'image d'une structure -- de sorte que le
journal survit à un changement de révision du logiciel dans les deux
sens.
