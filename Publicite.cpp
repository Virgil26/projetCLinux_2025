#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include "protocole.h" // contient la cle et la structure d'un message

int idQ, idShm;
int fd;

int main()
{
  // Armement des signaux

  // Masquage de SIGINT
  sigset_t mask;
  sigaddset(&mask,SIGINT);
  sigprocmask(SIG_SETMASK,&mask,NULL);

  // Recuperation de l'identifiant de la file de messages
  fprintf(stderr,"(PUBLICITE %d) Recuperation de l'id de la file de messages\n",getpid());
  
  // *** ETAPE 4 : AJOUT ***
  if((idQ = msgget(CLE, 0)) == -1)
  {
    perror("(PUBLICITE) Erreur de msgget");
    exit(1);
  }

  // Recuperation de l'identifiant de la mémoire partagée
  fprintf(stderr,"(PUBLICITE %d) Recuperation de l'id de la mémoire partagée\n",getpid());
  
  // *** ETAPE 4 : AJOUT ***
  if((idShm = shmget(CLE, 200, 0)) == -1)
  {
    perror("(PUBLICITE) Erreur de shmget");
    exit(1);
  }

  // Attachement à la mémoire partagée
  // *** ETAPE 4 : AJOUT ***
  char* zonePartagee = (char*) shmat(idShm,NULL,0); 
  if (zonePartagee == (char*) -1)
  {
    perror("(PUBLICITE) Erreur de shmat");
    exit(1);
  }

  // Ouverture du fichier de publicité
  // *** ETAPE 4 : AJOUT ***
  fd = open("publicites.dat",O_RDONLY);          
  if (fd == -1)
  {
    perror("(PUBLICITE) Erreur d'ouverture de publicites.dat");
    exit(1);
  }
  while(1)
  {
    PUBLICITE pub;

    // Lecture d'une publicité dans le fichier
    // *** ETAPE 4 : AJOUT ***
    int n = read(fd,&pub,sizeof(PUBLICITE));        
    if (n == 0)    // fin de fichier : on repart au debut
    {
      lseek(fd,0,SEEK_SET);
      n = read(fd,&pub,sizeof(PUBLICITE));
    }
    if (n != sizeof(PUBLICITE))
    {
      perror("(PUBLICITE) Erreur de lecture de publicites.dat");
      exit(1);
    }

    // Ecriture en mémoire partagée
    // *** ETAPE 4 : AJOUT ***
    strcpy(zonePartagee,pub.texte);

    // Envoi d'une requete UPDATE_PUB au serveur
    // *** ETAPE 4 : AJOUT ***
    MESSAGE m;
    m.type = 1; // 1 = destination "Serveur"
    m.expediteur = getpid();
    m.requete = UPDATE_PUB;
    msgsnd(idQ,&m,sizeof(MESSAGE)-sizeof(long),0);

    sleep(pub.nbSecondes); // ETAPE 4 - AJOUT : attente avant la pub suivante

  }
}

