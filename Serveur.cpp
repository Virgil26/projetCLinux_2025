#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <mysql.h>
#include <setjmp.h>
#include "protocole.h" // contient la cle et la structure d'un message
#include "FichierUtilisateur.h"   // AJOUTÉ - ETAPE 1b : pour utiliser estPresent/hash/ajouteUtilisateur/verifieMotDePasse

int idQ,idShm,idSem;
// *** ETAPE 5 : AJOUT ***
union semun   // union = struct où tous les membres se partagent le même emplacement mémoire.
{
  int val;
  struct semid_ds *buf;
  unsigned short *array;
};

TAB_CONNEXIONS *tab;

void afficheTab();
int trouverConnexionParPid(int pid);                        // AJOUTE - ETAPE 1a : déclaration; utilisée pour retrouver une fenêtre lors d'un DECONNECT/LOGIN∕LOGOUT
int trouverConnexionParNom(const char* nom);                // AJOUTE - ETAPE 2 : déclaration; utilisée par ACCEPT_USER/REFUSE_USER
int trouverConnexionLibre();                                // AJOUTE - ETAPE 1 : déclaration ; utilisée pour trouver une place libre lors d'un CONNECT
void notifieAjout(int pidDest, const char* nomAjoute);      // AJOUTE - ETAPE 2 : déclaration; utilisée par LOGIN
void notifieRetrait(int pidDest, const char* nomRetire);    // AJOUTE - ETAPE 2 : déclaration; utilisée par LOGOUT
void handlerSIGINT(int sig);                                // AJOUTE - ETAPE 1 : déclaration du handler de nettoyage au CTRL-C
void handlerSIGCHLD(int sig);                               // AJOUTE - ETAPE 5 : declaration du handler qui recolte les processus Consultation/Modification termines
void nettoieRessources();                                   // AJOUTE - ETAPE 5 : declaration ; factorise le nettoyage IPC/BD/Publicite (CTRL-C et erreur fatale de msgrcv)
MYSQL* connexion;


int main()
{
  // Connection à la BD
  connexion = mysql_init(NULL);
  if (mysql_real_connect(connexion,"localhost","Student","PassStudent1_","PourStudent",0,0,0) == NULL)
  {
    fprintf(stderr,"(SERVEUR) Erreur de connexion à la base de données...\n");
    exit(1);  
  }

  // Armement des signaux

  // *** ÉTAPE 1c - DÉBUT AJOUT ***
  // But : un <CTRL-C> doit "entrer dans un handler de signal dans lequel il supprime proprement la file de messages et ferme la connexion à la BD"(énoncé étape 1.c). 
  // Sans ça, CTRL-C tuait le process brutalement et laissait la file de messages "polluée" dans le système (visible avec ipcs).
  struct sigaction saInt;
  saInt.sa_handler = handlerSIGINT;
  sigemptyset(&saInt.sa_mask);
  saInt.sa_flags = 0;
  sigaction(SIGINT,&saInt,NULL);
  // *** ÉTAPE 1c - FIN AJOUT ***


  // *** ETAPE 5 - AJOUT ***
  // But : eviter l'accumulation de processus zombies (Consultation, et plus tard Modification) : des qu'un processus fils se termine, le noyau envoie SIGCHLD.
  // Attention : sur Linux, msgrcv/msgsnd/semop ne sont JAMAIS relances automatiquement apres une interruption par signal (contrairement a SA_RESTART pour d'autres appels).
  // Le msgrcv bloquant de la boucle principale sera donc interrompu (errno=EINTR) a
  // chaque fois qu'un enfant se termine : c'est gere explicitement plus bas.  
  struct sigaction saChld;
  saChld.sa_handler = handlerSIGCHLD;
  sigemptyset(&saChld.sa_mask);
  saChld.sa_flags = 0;
  sigaction(SIGCHLD,&saChld,NULL);
  // *** ETAPE 5 : FIN AJOUT ***

  // Creation des ressources
  fprintf(stderr, "(SERVEUR %d) Creation de la file de message\n", getpid());
  if((idQ = msgget(CLE, IPC_CREAT | IPC_EXCL | 0600)) == -1)   //CLE definie dans protocole.h
  {
    perror("(SERVEUR) Erreur de msgget");
    exit(1);
  }
  // *** ETAPE 4 : AJOUT ***
  fprintf(stderr,"(SERVEUR %d) Creation de la memoire partagee pour la Publicite\n",getpid());
  if ((idShm = shmget(CLE,200,IPC_CREAT | IPC_EXCL | 0600)) == -1) 
  {
    perror("(SERVEUR) Erreur de shmget");
    msgctl(idQ, IPC_RMID, NULL);
    exit(1);
  }

  // *** ETAPE 5 - AJOUT ***
  fprintf(stderr, "(SERVEUR %d) Creation du semaphore d'exclusion BD\n", getpid());
  if((idSem = semget(CLE,1,IPC_CREAT | IPC_EXCL | 0600)) == -1)
  {
    perror("(SERVEUR) Erreur de semget");
    msgctl(idQ, IPC_RMID, NULL);
    shmctl(idShm, IPC_RMID, NULL);
    exit(1);
  }
  union semun argSem;
  argSem.val = 1;   // libre au départ : pas de modification en cours
  semctl(idSem,0,SETVAL, argSem);
  // *** ETAPE 5 : FIN AJOUT ***

  // Initialisation du tableau de connexions
  fprintf(stderr,"(SERVEUR %d) Initialisation de la table des connexions\n",getpid());
  tab = (TAB_CONNEXIONS*) malloc(sizeof(TAB_CONNEXIONS)); 

  for (int i=0 ; i<6 ; i++)
  {
    tab->connexions[i].pidFenetre = 0;
    strcpy(tab->connexions[i].nom,"");
    for (int j=0 ; j<5 ; j++) tab->connexions[i].autres[j] = 0;
    tab->connexions[i].pidModification = 0;
  }
  tab->pidServeur1 = getpid();
  tab->pidServeur2 = 0;
  tab->pidAdmin = 0;
  tab->pidPublicite = 0;

  // *** ETAPE 4 : AJOUT ***
  pid_t pidPub = fork();
  if (pidPub == -1)
  {
    perror("(SERVEUR) Erreur de fork pour Publicite");
    exit(1);
  }
  if (pidPub == 0)
  {
    execl("./Publicite","Publicite",NULL);
    perror("(SERVEUR) Erreur d'exec de Publicite");
    exit(1);
  }
  tab->pidPublicite = pidPub;
  // *** ETAPE 4 : FIN AJOUT ***

  afficheTab();

  // Creation du processus Publicite

  int i,k,j;
  MESSAGE m;
  MESSAGE reponse;
  char requete[200];
  MYSQL_RES  *resultat;
  MYSQL_ROW  tuple;
  PUBLICITE pub;

  while(1)
  {
  	fprintf(stderr,"(SERVEUR %d) Attente d'une requete...\n",getpid());
    if (msgrcv(idQ,&m,sizeof(MESSAGE)-sizeof(long),1,0) == -1)
    {
      // *** ETAPE 5 - AJOUT ***
      // But : msgrcv est interrompu (EINTR) chaque fois que SIGCHLD arrive pendant l'attente (fin d'un Consultation/Modification) ; ce n'est pas une vraie
      // erreur, on relance simplement l'attente d'une requete.
      if(errno == EINTR) continue;

      perror("(SERVEUR) Erreur de msgrcv");
      //msgctl(idQ,IPC_RMID,NULL);    // remplacer plus tard par nettoieRessources();
      nettoieRessources();
      exit(1);
    }

    switch(m.requete)
    {
      case CONNECT :  
                      // *** ÉTAPE 1a - DÉBUT MODIF ***
                      // But : "le serveur recherche une ligne vide dans son tableau de connexions et l'insère" (énoncé étape 1.a).
                      // Avant : seul le fprintf existait, rien n'était stocké.
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete CONNECT reçue de %d\n",getpid(),m.expediteur);
                        int idx = trouverConnexionLibre();
                        if (idx == -1)
                          fprintf(stderr,"(SERVEUR %d) Table de connexions pleine : connexion refusee pour %d\n",getpid(),m.expediteur);
                        else
                          tab->connexions[idx].pidFenetre = m.expediteur;
                      }
                      break;
                      // *** ÉTAPE 1a - FIN MODIF  ***

      case DECONNECT :  
                      // *** ÉTAPE 1a - DÉBUT MODIF ***
                      // But : "le serveur supprime la fenêtre de sa table de
                      // connexions" (énoncé étape 1.a, clic sur la croix).
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete DECONNECT reçue de %d\n",getpid(),m.expediteur);
                        int idx = trouverConnexionParPid(m.expediteur);
                        if (idx != -1)
                        {
                          tab->connexions[idx].pidFenetre = 0;
                          strcpy(tab->connexions[idx].nom,"");
                          for (int j=0 ; j<5 ; j++) tab->connexions[idx].autres[j] = 0;
                          tab->connexions[idx].pidModification = 0;
                        }
                      }
                      break;
                      // *** ÉTAPE 1a - FIN MODIF ***

      case LOGIN :  
                      // ★★★ ÉTAPE 1b - DÉBUT MODIF (le plus gros morceau)
                      // But : "le serveur vérifie dans le fichier binaire utilisateurs.dat la présence de l'utilisateur et
                      // vérifie son mot de passe, ou alors il en crée un nouveau" puis répond OK/KO + SIGUSR1 (énoncé étape 1.b).
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete LOGIN reçue de %d : --%s--%s--%s--\n",getpid(),m.expediteur,m.data1,m.data2,m.texte);

                        int idx = trouverConnexionParPid(m.expediteur);
                        reponse.type = m.expediteur;
                        reponse.expediteur = getpid();
                        reponse.requete = LOGIN;

                        if (idx == -1)
                        {
                          // La fenetre n'est pas connue du serveur (ne devrait jamais arriver)
                          strcpy(reponse.data1,"KO");
                          strcpy(reponse.texte,"Erreur interne : fenetre inconnue du serveur.");
                        }
                        else if (strcmp(m.data1,"1") == 0) // nouvel utilisateur
                        {
                          if (estPresent(m.data2) > 0)
                          {
                            strcpy(reponse.data1,"KO");
                            sprintf(reponse.texte,"Un utilisateur nomme %s existe deja.",m.data2);
                          }
                          else
                          {
                            ajouteUtilisateur(m.data2,hash(m.texte));
                            // TODO etape 5 : ajouter le tuple (nom,'---','---') dans la table UNIX_FINAL
                            // *** ETAPE 5 : AJOUT ***
                            char requeteSQL[256];
                            sprintf(requeteSQL,"INSERT INTO UNIX_FINAL (nom,gsm,email) VALUES ('%s','---','---')",m.data2);
                            if (mysql_query(connexion,requeteSQL) != 0)
                              fprintf(stderr,"(SERVEUR) Erreur d'insertion BD pour %s : %s\n",m.data2,mysql_error(connexion));
                            // *** ETAPE 5 : FIN AJOUT ***
                            
                            strcpy(tab->connexions[idx].nom,m.data2);
                            strcpy(reponse.data1,"OK");
                            sprintf(reponse.texte,"Bienvenue %s, votre compte a ete cree !",m.data2);
                          }
                        }
                        else // utilisateur existant
                        {
                          int pos = estPresent(m.data2);
                          if (pos <= 0) // -1 : pas de fichier, 0 : pas trouve
                          {
                            strcpy(reponse.data1,"KO");
                            strcpy(reponse.texte,"Utilisateur inconnu.");
                          }
                          else if (verifieMotDePasse(pos,hash(m.texte)) == 1)
                          {
                            strcpy(tab->connexions[idx].nom,m.data2);
                            strcpy(reponse.data1,"OK");
                            sprintf(reponse.texte,"Bienvenue %s !",m.data2);
                          }
                          else
                          {
                            strcpy(reponse.data1,"KO");
                            strcpy(reponse.texte,"Mot de passe incorrect.");
                          }
                        }

                        msgsnd(idQ,&reponse,sizeof(MESSAGE)-sizeof(long),0);
                        kill(m.expediteur,SIGUSR1);

                        /* ***ETAPE 2.a - AJOUT ***
                        Si le login est reussi, on previendra tous les utilisateurs deja loggé (nom non vide, hotmis la ligne du nouvel arrivant) qu'un 
                        nouvel utilisateur est arrive (ADD_USER), et on previendra egalement le nouvel arrivant de chacun des utilisateurs déjà présents*/
                        if(strcmp(reponse.data1, "OK") == 0)
                        {
                          for(int i=0; i<6; i++)
                          {
                            if(i != idx && strlen(tab->connexions[i].nom) >0)
                            {
                              notifieAjout(tab->connexions[i].pidFenetre, m.data2);
                              notifieAjout(m.expediteur, tab->connexions[i].nom);
                            }
                          }
                        }
                        // *** ETAPE 2.a - FIN AJOUT ***

                      }
                      break;
                      // ★★★ ÉTAPE 1b - FIN MODIF 

      case LOGOUT :  
                      // *** ÉTAPE 1b - DÉBUT MODIF ***
                      // But : "un clic sur Logout doit envoyer une requête
                      // LOGOUT au serveur qui supprime l'utilisateur de sa
                      // table des connexions. Attention que le PID de la
                      // fenêtre est maintenu" (énoncé étape 1.b).
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete LOGOUT reçue de %d\n",getpid(),m.expediteur);
                        int idx = trouverConnexionParPid(m.expediteur);
                        if (idx != -1)
                        {
                          char nomDeloggue[20];
                          strcpy(nomDeloggue, tab->connexions[idx].nom);
                          /* *** ETAPE 2.a - AJOUT ***
                          on previent tous les autres utilisateurs encore connecté que cet utilisateur vient de se déconnecter (REMOVE_USER), 
                          AVANT d'effacer son nom de la table*/
                          for (int i=0; i<6; i++)
                          {
                            if(i!=idx && strlen(tab->connexions[i].nom) > 0)
                              notifieRetrait(tab->connexions[i].pidFenetre, nomDeloggue);
                          }
                          // *** ETAPE 2.a - FIN AJOUT ***

                          strcpy(tab->connexions[idx].nom,"");
                          for (int j=0 ; j<5 ; j++) tab->connexions[idx].autres[j] = 0;
                          // pidFenetre n'est PAS remis a 0 : la fenetre reste connectee au serveur
                          
                          /* *** ETAPE 2.b - AJOUT ***
                          le pid de la fenetre delogge doit egalement disparaitre du champ "autres" de tous les autres utilisateurs (au cas ou ils l'avaient accepte)*/
                          for(int i=0 ; i<6 ; i++)
                          {
                            for(int j=0 ; j<5 ; j++)
                            {
                              if(tab->connexions[i].autres[j] == m.expediteur)
                                tab->connexions[i].autres[j] = 0;
                            }
                          }
                          // *** ETAPE 2.b - FIN AJOUT ***
                        }
                      }
                      break;
                      // *** ÉTAPE 1b - FIN MODIF ***

      case ACCEPT_USER :
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete ACCEPT_USER reçue de %d\n",getpid(),m.expediteur);
                        int idx = trouverConnexionParPid(m.expediteur);
                        int idxCible = trouverConnexionParNom(m.data1);
                        if (idx != -1 && idxCible != -1)
                        {
                          int pidCible = tab->connexions[idxCible].pidFenetre;
                          int dejaPresent = 0;
                          int caseLibre = -1;
                          for (int j=0 ; j<5 ; j++)
                          {
                            if (tab->connexions[idx].autres[j] == pidCible) dejaPresent = 1;
                            if (caseLibre == -1 && tab->connexions[idx].autres[j] == 0) caseLibre = j;
                          }
                          if (!dejaPresent && caseLibre != -1)
                            tab->connexions[idx].autres[caseLibre] = pidCible;
                        }
                      }
                      break;

      case REFUSE_USER :
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete REFUSE_USER reçue de %d\n",getpid(),m.expediteur);
                        int idx = trouverConnexionParPid(m.expediteur);
                        int idxCible = trouverConnexionParNom(m.data1);
                        if (idx != -1 && idxCible != -1)
                        {
                          int pidCible = tab->connexions[idxCible].pidFenetre;
                          for (int j=0 ; j<5 ; j++)
                            if (tab->connexions[idx].autres[j] == pidCible)
                              tab->connexions[idx].autres[j] = 0;
                        }
                      }
                      break;

      case SEND :  
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete SEND reçue de %d : --%s--\n",getpid(),m.expediteur,m.texte);
                        int idx = trouverConnexionParPid(m.expediteur);
                        if (idx != -1)
                        {
                          MESSAGE msgEnvoi;
                          msgEnvoi.expediteur = getpid();
                          msgEnvoi.requete = SEND;
                          strcpy(msgEnvoi.data1,tab->connexions[idx].nom);
                          strcpy(msgEnvoi.texte,m.texte);

                          for (int j=0 ; j<5 ; j++)
                          {
                            int pidDest = tab->connexions[idx].autres[j];
                            if (pidDest != 0)
                            {
                              msgEnvoi.type = pidDest;
                              msgsnd(idQ,&msgEnvoi,sizeof(MESSAGE)-sizeof(long),0);
                              kill(pidDest,SIGUSR1);
                            }
                          }
                        }
                      }
                      break;

      case UPDATE_PUB :
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete UPDATE_PUB reçue de %d\n",getpid(),m.expediteur);
                        for (int i=0 ; i<6 ; i++)
                          if (tab->connexions[i].pidFenetre != 0)
                            kill(tab->connexions[i].pidFenetre,SIGUSR2);
                      }
                      break;

      case CONSULT :
                      {
                        fprintf(stderr,"(SERVEUR %d) Requete CONSULT reçue de %d : --%s--\n",getpid(),m.expediteur,m.data1);
                        pid_t pidConsult = fork();
                        if (pidConsult == -1)
                          perror("(SERVEUR) Erreur de fork pour Consultation");
                        else if (pidConsult == 0)
                        {
                          execl("./Consultation","Consultation",NULL);
                          perror("(SERVEUR) Erreur d'exec de Consultation");
                          exit(1);
                        }
                        else
                        {
                          MESSAGE msgVersConsult;
                          msgVersConsult.type = pidConsult;
                          msgVersConsult.expediteur = m.expediteur;   // pid du Client a qui Consultation devra repondre
                          msgVersConsult.requete = CONSULT;
                          strcpy(msgVersConsult.data1,m.data1);       // nom recherche
                          msgsnd(idQ,&msgVersConsult,sizeof(MESSAGE)-sizeof(long),0);
                        }
                      }
                      break;

      case MODIF1 :
                    {
                      fprintf(stderr,"(SERVEUR %d) Requete MODIF1 reçue de %d\n",getpid(),m.expediteur);
                      int idx = trouverConnexionParPid(m.expediteur);
                      if (idx == -1)
                      {
                        fprintf(stderr,"(SERVEUR %d) MODIF1 : fenetre inconnue pour %d\n",getpid(),m.expediteur);
                        break;
                      }
                      pid_t pidModif = fork();
                      if (pidModif == -1)
                        perror("(SERVEUR) Erreur de fork pour Modification");
                      else if (pidModif == 0)
                      {
                        execl("./Modification","Modification",NULL);
                        perror("(SERVEUR) Erreur d'exec de Modification");
                        exit(1);
                      }
                      else
                      {
                        tab->connexions[idx].pidModification = pidModif;
                        MESSAGE msgVersModif;
                        msgVersModif.type = pidModif;
                        msgVersModif.expediteur = m.expediteur;              // pid du Client a qui Modification devra repondre
                        msgVersModif.requete = MODIF1;
                        strcpy(msgVersModif.data1,tab->connexions[idx].nom); // nom de l'utilisateur loggue sur cette fenetre
                        msgsnd(idQ,&msgVersModif,sizeof(MESSAGE)-sizeof(long),0);
                      }
                    }
                      break;

      case MODIF2 :
                    {
                      fprintf(stderr,"(SERVEUR %d) Requete MODIF2 reçue de %d\n",getpid(),m.expediteur);
                      int idx = trouverConnexionParPid(m.expediteur);
                      if (idx != -1 && tab->connexions[idx].pidModification > 0)
                      {
                        MESSAGE msgVersModif;
                        msgVersModif.type = tab->connexions[idx].pidModification;
                        msgVersModif.expediteur = m.expediteur;
                        msgVersModif.requete = MODIF2;
                        strcpy(msgVersModif.data1,m.data1);   // nouveau mot de passe (eventuellement vide)
                        strcpy(msgVersModif.data2,m.data2);   // gsm modifie
                        strcpy(msgVersModif.texte,m.texte);   // email modifie
                        msgsnd(idQ,&msgVersModif,sizeof(MESSAGE)-sizeof(long),0);
                      }
                    }
                      break;

      case LOGIN_ADMIN :
                      fprintf(stderr,"(SERVEUR %d) Requete LOGIN_ADMIN reçue de %d\n",getpid(),m.expediteur);
                      break;

      case LOGOUT_ADMIN :
                      fprintf(stderr,"(SERVEUR %d) Requete LOGOUT_ADMIN reçue de %d\n",getpid(),m.expediteur);
                      break;

      case NEW_USER :
                      fprintf(stderr,"(SERVEUR %d) Requete NEW_USER reçue de %d : --%s--%s--\n",getpid(),m.expediteur,m.data1,m.data2);
                      break;

      case DELETE_USER :
                      fprintf(stderr,"(SERVEUR %d) Requete DELETE_USER reçue de %d : --%s--\n",getpid(),m.expediteur,m.data1);
                      break;

      case NEW_PUB :
                      fprintf(stderr,"(SERVEUR %d) Requete NEW_PUB reçue de %d\n",getpid(),m.expediteur);
                      break;
    }
    afficheTab();
  }
}

void afficheTab()
{
  fprintf(stderr,"Pid Serveur 1 : %d\n",tab->pidServeur1);
  fprintf(stderr,"Pid Serveur 2 : %d\n",tab->pidServeur2);
  fprintf(stderr,"Pid Publicite : %d\n",tab->pidPublicite);
  fprintf(stderr,"Pid Admin     : %d\n",tab->pidAdmin);
  for (int i=0 ; i<6 ; i++)
    fprintf(stderr,"%6d -%20s- %6d %6d %6d %6d %6d - %6d\n",tab->connexions[i].pidFenetre,
                                                      tab->connexions[i].nom,
                                                      tab->connexions[i].autres[0],
                                                      tab->connexions[i].autres[1],
                                                      tab->connexions[i].autres[2],
                                                      tab->connexions[i].autres[3],
                                                      tab->connexions[i].autres[4],
                                                      tab->connexions[i].pidModification);
  fprintf(stderr,"\n");
}

/////////////////////////////////////////////////////////////////////////////
// *** ÉTAPE 1a - AJOUTÉ ***
/* But : outil utilisé par DECONNECT (1a) et aussi par LOGIN/LOGOUT (1b).
   Recherche dans le tableau de connexions la ligne dont le pid de fenetre
   vaut "pid". Renvoie l'indice (0..5) ou -1 si aucune ligne ne correspond.*/
int trouverConnexionParPid(int pid)
{
  for (int i=0 ; i<6 ; i++)
    if (tab->connexions[i].pidFenetre == pid) return i;
  return -1;
}

/////////////////////////////////////////////////////////////////////////////
// *** ÉTAPE 1a - AJOUTÉ ***
/* But : outil utilisé par CONNECT pour trouver une place libre dans le tableau de connexions (6 fenêtres max, voir énoncé étape 1.a).
   Renvoie son indice (0..5) ou -1 si le tableau est plein. */
int trouverConnexionLibre()
{
  for (int i=0 ; i<6 ; i++)
    if (tab->connexions[i].pidFenetre == 0) return i;
  return -1;
}

/////////////////////////////////////////////////////////////////////////////
// ETAPE 2 : recherche dans le tableau de connexions la ligne dont le nom (utilisateur loggue) vaut "nom". 
// Une ligne dont le nom est vide (fenetre connectee mais pas encore loggue, ou libre) ne peut jamais correspondre.
// Renvoie l'indice (0..5) ou -1 si aucun utilisateur logge ne porte ce nom.
int trouverConnexionParNom(const char* nom)
{
  if (strlen(nom) == 0) return -1;
  for (int i=0 ; i<6 ; i++)
    if (strlen(tab->connexions[i].nom) > 0 && strcmp(tab->connexions[i].nom,nom) == 0) return i;
  return -1;
}

/////////////////////////////////////////////////////////////////////////////
// ETAPE 2 : envoie un message ADD_USER au pid "pidDest" pour lui signaler qu'un utilisateur nomme "nomAjoute" est desormais loggue (soit un nouvel
// arrivant a annoncer aux autres, soit les autres a annoncer au nouvel arrivant), puis reveille ce pid avec SIGUSR1.
void notifieAjout(int pidDest, const char* nomAjoute)
{
  MESSAGE msg;
  msg.type = pidDest;
  msg.expediteur = getpid();
  msg.requete = ADD_USER;
  strcpy(msg.data1,nomAjoute);
  msgsnd(idQ,&msg,sizeof(MESSAGE)-sizeof(long),0);
  kill(pidDest,SIGUSR1);
}

/////////////////////////////////////////////////////////////////////////////
// ETAPE 2 : envoie un message REMOVE_USER au pid "pidDest" pour lui signaler
// que l'utilisateur nomme "nomRetire" vient de se delogger, puis reveille ce
// pid avec SIGUSR1.
void notifieRetrait(int pidDest, const char* nomRetire)
{
  MESSAGE msg;
  msg.type = pidDest;
  msg.expediteur = getpid();
  msg.requete = REMOVE_USER;
  strcpy(msg.data1,nomRetire);
  msgsnd(idQ,&msg,sizeof(MESSAGE)-sizeof(long),0);
  kill(pidDest,SIGUSR1);
}
/////////////////////////////////////////////////////////////////////////////
// *** ETAPE 5 - AJOUTE ***
// But : factorise le nettoyage des ressources IPC et de la connexion BD,
// utilise a la fois par l'arret volontaire (CTRL-C, handlerSIGINT) et par
// un arret force en cas d'erreur fatale de msgrcv dans la boucle principale.
void nettoieRessources()
{
  msgctl(idQ,IPC_RMID,NULL);
  shmctl(idShm,IPC_RMID,NULL);
  semctl(idSem,0,IPC_RMID,0);
  mysql_close(connexion);
  // On tue aussi le processus "Publicite" pour eviter qu'il ne reste attache au segment de memoire partagee (ce qui empecherait sa suppression
  // definitive meme apres le shmctl(IPC_RMID) ci-dessus).
  // Attention : kill(0,...) enverrait le signal a TOUT le groupe de processus (y compris le Serveur lui-meme) ; on verifie donc que
  // pidPublicite est bien renseigne avant d'appeler kill.
  if (tab != NULL && tab->pidPublicite > 0)
    kill(tab->pidPublicite,SIGTERM);
}
/////////////////////////////////////////////////////////////////////////////
// *** ÉTAPE 1c - AJOUTÉ ***
// But : "supprime proprement la file de messages et ferme la connexion à la base de données" lors d'un <CTRL-C> (énoncé étape 1.c).
void handlerSIGINT(int sig)
{
  (void) sig;
  fprintf(stderr,"\n(SERVEUR %d) Arret demande (CTRL-C), nettoyage des ressources...\n",getpid());
  nettoieRessources();
  exit(0);
} 


/////////////////////////////////////////////////////////////////////////////
// *** ETAPE 5 - AJOUTE ***
/* BUT : recolte les processus fils (Consultation, plus tard Modification) qui
se sont termines, pour eviter qu'ils ne restent en zombies. SIGCHLD n'est
pas mis en file par le noyau (comme SIGUSR1 cote Client) : si plusieurs
enfants se terminent presque en meme temps, un seul reveil du handler peut
se produire. On vide donc TOUTES les terminaisons en attente avec une
boucle waitpid(...,WNOHANG), jamais un seul appel.*/
void handlerSIGCHLD(int sig)
{
  (void) sig;
  pid_t pidTermine;
  while ((pidTermine = waitpid(-1,NULL,WNOHANG)) > 0)
  {
    fprintf(stderr,"(SERVEUR %d) Processus %d termine (recolte)\n",getpid(),pidTermine);
    // Si ce pid correspondait a une Modification en cours pour un utilisateur,
    // on libere la ligne correspondante dans la table de connexions.
    for (int i=0 ; i<6 ; i++)
      if (tab->connexions[i].pidModification == pidTermine)
        tab->connexions[i].pidModification = 0;
  }
}