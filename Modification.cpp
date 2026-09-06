#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <mysql.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "protocole.h"
#include "FichierUtilisateur.h"   // ETAPE 5 - AJOUT : pour modifieMotDePasse/estPresent/hash (changement de mot de passe)

int idQ,idSem;

int main()
{
  char nom[40];

  // Recuperation de l'identifiant de la file de messages
  fprintf(stderr,"(MODIFICATION %d) Recuperation de l'id de la file de messages\n",getpid());
  if ((idQ = msgget(CLE,0)) == -1)
  {
    perror("(MODIFICATION) Erreur de msgget");
    exit(1);
  }

  // Recuperation de l'identifiant du sémaphore
  fprintf(stderr,"(MODIFICATION %d) Recuperation de l'id du sémaphore\n",getpid());
  if ((idSem = semget(CLE,1,0)) == -1)
  {
    perror("(MODIFICATION) Erreur de semget");
    exit(1);
  }

  MESSAGE m;
  // Lecture de la requête MODIF1
  fprintf(stderr,"(MODIFICATION %d) Lecture requete MODIF1\n",getpid());
  // *** ETAPE 5 - AJOUT ***
  msgrcv(idQ, &m, sizeof(MESSAGE)-sizeof(long), getpid(), 0);
  strcpy(nom, m.data1);   // nom de l'utilisateur logge, transmis par le Serveur

  // Tentative de prise non bloquante du semaphore 0 (au cas où un autre utilisateut est déjà en train de modifier)
  fprintf(stderr,"(MODIFICATION %d) Prise non bloquante du sémaphore 0\n",getpid());
  struct sembuf opPrise;
  opPrise.sem_num = 0;
  opPrise.sem_op = -1;
  opPrise.sem_flg = IPC_NOWAIT;
  if (semop(idSem,&opPrise,1) == -1)
  {
    // Semaphore indisponible (errno == EAGAIN) : une modification est deja en cours ailleurs. On ne l'a PAS acquis, donc on ne doit surtout pas le
    // relacher (semop +1) plus loin : ca romprait l'exclusion mutuelle pour tout le monde. 
    // On repond directement KO dans les 3 champs et on se termine, sans jamais se connecter a la BD.
    fprintf(stderr,"(MODIFICATION %d) Semaphore indisponible : modification deja en cours\n",getpid());
    MESSAGE reponseKO;
    reponseKO.type = m.expediteur;
    reponseKO.expediteur = getpid();
    reponseKO.requete = MODIF1;
    strcpy(reponseKO.data1,"KO");
    strcpy(reponseKO.data2,"KO");
    strcpy(reponseKO.texte,"KO");
    msgsnd(idQ,&reponseKO,sizeof(MESSAGE)-sizeof(long),0);
    exit(0);
  }

  // Connexion à la base de donnée
  MYSQL *connexion = mysql_init(NULL);
  fprintf(stderr,"(MODIFICATION %d) Connexion à la BD\n",getpid());
  if (mysql_real_connect(connexion,"localhost","Student","PassStudent1_","PourStudent",0,0,0) == NULL)
  {
    fprintf(stderr,"(MODIFICATION) Erreur de connexion à la base de données...\n");
    // *** ETAPE 5 - AJOUT ***
    // Si on arrive dans ce cas-ci cela signifie que la connexion a échoué. On doit fermer le processus ET le sémaphore
    struct sembuf opLibereErreur;
    opLibereErreur.sem_num = 0;
    opLibereErreur.sem_op = 1;
    opLibereErreur.sem_flg = 0;
    semop(idSem,&opLibereErreur,1);

    exit(1);  
  }

  // Recherche des infos dans la base de données
  fprintf(stderr,"(MODIFICATION %d) Consultation en BD pour --%s--\n",getpid(),m.data1);
  // strcpy(nom,m.data1); // --> nom a déjà été remplis plus haut dans "lecture de la requete MODIF1"
  MYSQL_RES  *resultat;
  MYSQL_ROW  tuple;
  char requete[200];
  sprintf(requete,"SELECT gsm,email FROM UNIX_FINAL WHERE nom='%s'",nom);
  mysql_query(connexion,requete);
  resultat = mysql_store_result(connexion);
  tuple = mysql_fetch_row(resultat); // l'utilisateur existe forcement (tuple créer au LOGIN)
  
  // *** ETAPE 5 - AJOUT : GARDE-FOU ***
  // Garde-fou : si l'utilisateur n'a jamais ete insere dans UNIX_FINAL (compte
  // cree avant l'ajout du INSERT au LOGIN), tuple est NULL : on evite le crash
  // et on repond KO plutot que de planter sur tuple[0].
  if (tuple == NULL)
  {
    fprintf(stderr,"(MODIFICATION %d) Aucune ligne UNIX_FINAL pour --%s--\n",getpid(),nom);
    MESSAGE reponseKO;
    reponseKO.type = m.expediteur;
    reponseKO.expediteur = getpid();
    reponseKO.requete = MODIF1;
    strcpy(reponseKO.data1,"KO");
    strcpy(reponseKO.data2,"KO");
    strcpy(reponseKO.texte,"KO");
    msgsnd(idQ,&reponseKO,sizeof(MESSAGE)-sizeof(long),0);
    mysql_free_result(resultat);
    mysql_close(connexion);
    struct sembuf opLibereErreur;
    opLibereErreur.sem_num = 0;
    opLibereErreur.sem_op = 1;
    opLibereErreur.sem_flg = 0;
    semop(idSem,&opLibereErreur,1);
    exit(0);
  }
  // *** ETAPE 5 : GARDE-FOU ***

  // Construction et envoi de la reponse
  fprintf(stderr,"(MODIFICATION %d) Envoi de la reponse\n",getpid());
  MESSAGE reponse;
  reponse.type = m.expediteur;
  reponse.expediteur = getpid();
  reponse.requete = MODIF1;
  strcpy(reponse.data1,"OK");
  strcpy(reponse.data2,tuple[0]);   // gsm actuel
  strcpy(reponse.texte,tuple[1]);   // email actuel
  msgsnd(idQ,&reponse,sizeof(MESSAGE)-sizeof(long),0);
  mysql_free_result(resultat);
  
  // Attente de la requête MODIF2
  fprintf(stderr,"(MODIFICATION %d) Attente requete MODIF2...\n",getpid());
  MESSAGE m2;
  msgrcv(idQ,&m2,sizeof(MESSAGE)-sizeof(long),getpid(),0);

  // Mise à jour base de données
  fprintf(stderr,"(MODIFICATION %d) Modification en base de données pour --%s--\n",getpid(),nom);
  sprintf(requete,"UPDATE UNIX_FINAL SET gsm='%s', email='%s' WHERE nom='%s'",m2.data2,m2.texte,nom);
  mysql_query(connexion,requete); 

  // Mise à jour du fichier utilisateurs.dat si un nouveau mot de passe a ete fourni
  // (champ vide -> l'utilisateur ne veut pas changer son mot de passe, cf. enonce)
  if (strlen(m2.data1) > 0)
  {
    int position = estPresent(nom);
    if (position > 0)
      modifieMotDePasse(position,hash(m2.data1));
  }

  // Deconnexion BD
  mysql_close(connexion);

  // Libération du semaphore 0
  fprintf(stderr,"(MODIFICATION %d) Libération du sémaphore 0\n",getpid());
  struct sembuf opLibere;
  opLibere.sem_num = 0;
  opLibere.sem_op = 1;
  opLibere.sem_flg = 0;
  semop(idSem,&opLibere,1);

  exit(0);
}