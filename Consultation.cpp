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

int idQ,idSem;

int main()
{
  // Recuperation de l'identifiant de la file de messages
  fprintf(stderr,"(CONSULTATION %d) Recuperation de l'id de la file de messages\n",getpid());
  if((idQ = msgget(CLE, 0)) == -1)
  {
    perror("(CONSULTATION) Erreur de msgget");
    exit(1);
  }

  // Recuperation de l'identifiant du sémaphore
  fprintf(stderr,"(CONSULTATION %d) Recuperation de l'id du semaphore\n",getpid());
  if ((idSem = semget(CLE,1,0)) == -1)
  {
    perror("(CONSULTATION) Erreur de semget");
    exit(1);
  }

  MESSAGE m;
  // Lecture de la requête CONSULT
  fprintf(stderr,"(CONSULTATION %d) Lecture requete CONSULT\n",getpid());
  msgrcv(idQ,&m,sizeof(MESSAGE)-sizeof(long),getpid(),0);

  // Tentative de prise bloquante du semaphore 0
  fprintf(stderr,"(CONSULTATION %d) Prise bloquante du sémaphore 0\n",getpid());
  struct sembuf opPrise;
  opPrise.sem_num = 0;
  opPrise.sem_op = -1;
  opPrise.sem_flg = 0;
  semop(idSem,&opPrise,1);

  // Connexion à la base de donnée
  MYSQL *connexion = mysql_init(NULL);
  fprintf(stderr,"(CONSULTATION %d) Connexion à la BD\n",getpid());
  if (mysql_real_connect(connexion,"localhost","Student","PassStudent1_","PourStudent",0,0,0) == NULL)
  {
    fprintf(stderr,"(CONSULTATION) Erreur de connexion à la base de données...\n");
    exit(1);  
  }

  // Recherche des infos dans la base de données
  fprintf(stderr,"(CONSULTATION %d) Consultation en BD (%s)\n",getpid(),m.data1);
  MYSQL_RES  *resultat;
  MYSQL_ROW  tuple;
  char requete[200];

  // sprintf(requete,...);
  // *** ETAPE 5 - AJOUT ***
  sprintf(requete,"SELECT gsm,email FROM UNIX_FINAL WHERE nom='%s'",m.data1);
  
  mysql_query(connexion,requete),
  resultat = mysql_store_result(connexion);

  // *** ETAPE 5 - AJOUT ***
  //Construction de la réponse
  MESSAGE reponse;
  reponse.type = m.expediteur;   //pid du Client, transmis par le Serveur
  reponse.expediteur = getpid();
  reponse.requete = CONSULT;

  // if ((tuple = mysql_fetch_row(resultat)) != NULL) ...
  if(resultat != NULL && (tuple = mysql_fetch_row(resultat)) != NULL)
  {
    strcpy(reponse.data1, "OK");
    strcpy(reponse.data2, tuple[0]);
    strcpy(reponse.texte, tuple[1]);
  }
  else{
    strcpy(reponse.data1, "KO");
  }
  if(resultat != NULL)
    mysql_free_result(resultat);

  // Envoi de la reponse
  msgsnd(idQ, &reponse, sizeof(MESSAGE)-sizeof(long), 0);
  kill(m.expediteur, SIGUSR1);

  // Deconnexion BD
  mysql_close(connexion);

  // Libération du semaphore 0
  fprintf(stderr,"(CONSULTATION %d) Libération du sémaphore 0\n",getpid());
  struct sembuf opLibere;
  opLibere.sem_num = 0;
  opLibere.sem_op = 1;
  opLibere.sem_flg = 0;
  semop(idSem, &opLibere, 1);

  exit(0);
}