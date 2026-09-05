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
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <mysql.h>
#include <setjmp.h>
#include "protocole.h" // contient la cle et la structure d'un message
#include "FichierUtilisateur.h"   // AJOUTÉ - ETAPE 1b : pour utiliser estPresent/hash/ajouteUtilisateur/verifieMotDePasse

int idQ,idShm,idSem;
TAB_CONNEXIONS *tab;

void afficheTab();
int trouverConnexionParPid(int pid);    // AJOUTE - ETAPE 1a : déclaration; utilisée pour retrouver une fenêtre lors d'un DECONNECT/LOGIN∕LOGOUT
int trouverConnexionLibre();    // *** AJOUTE : déclaration ; utilisée pour trouver une place libre lors d'un CONNECT
void handlerSIGINT(int sig);    // *** AJOUTE : déclaration du handler de nettoyage au CTRL-C

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

  // Creation des ressources
  fprintf(stderr,"(SERVEUR %d) Creation de la file de messages\n",getpid());
  if ((idQ = msgget(CLE,IPC_CREAT | IPC_EXCL | 0600)) == -1)  // CLE definie dans protocole.h
  {
    perror("(SERVEUR) Erreur de msgget");
    exit(1);
  }

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
      perror("(SERVEUR) Erreur de msgrcv");
      msgctl(idQ,IPC_RMID,NULL);
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
                          strcpy(tab->connexions[idx].nom,"");
                          for (int j=0 ; j<5 ; j++) tab->connexions[idx].autres[j] = 0;
                          // pidFenetre n'est PAS remis a 0 : la fenetre reste connectee au serveur
                        }
                      }
                      break;
                      // *** ÉTAPE 1b - FIN MODIF ***

      case ACCEPT_USER :
                      fprintf(stderr,"(SERVEUR %d) Requete ACCEPT_USER reçue de %d\n",getpid(),m.expediteur);
                      break;

      case REFUSE_USER :
                      fprintf(stderr,"(SERVEUR %d) Requete REFUSE_USER reçue de %d\n",getpid(),m.expediteur);
                      break;

      case SEND :  
                      fprintf(stderr,"(SERVEUR %d) Requete SEND reçue de %d\n",getpid(),m.expediteur);
                      break; 

      case UPDATE_PUB :
                      fprintf(stderr,"(SERVEUR %d) Requete UPDATE_PUB reçue de %d\n",getpid(),m.expediteur);
                      break;

      case CONSULT :
                      fprintf(stderr,"(SERVEUR %d) Requete CONSULT reçue de %d\n",getpid(),m.expediteur);
                      break;

      case MODIF1 :
                      fprintf(stderr,"(SERVEUR %d) Requete MODIF1 reçue de %d\n",getpid(),m.expediteur);
                      break;

      case MODIF2 :
                      fprintf(stderr,"(SERVEUR %d) Requete MODIF2 reçue de %d\n",getpid(),m.expediteur);
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
// ★★★ ÉTAPE 1a - AJOUTÉ
/* But : outil utilisé par CONNECT pour trouver une place libre dans le tableau de connexions (6 fenêtres max, voir énoncé étape 1.a).
   Renvoie son indice (0..5) ou -1 si le tableau est plein. */
int trouverConnexionLibre()
{
  for (int i=0 ; i<6 ; i++)
    if (tab->connexions[i].pidFenetre == 0) return i;
  return -1;
}

/////////////////////////////////////////////////////////////////////////////
// ★★★ ÉTAPE 1c - AJOUTÉ
// But : "supprime proprement la file de messages et ferme la connexion à la base de données" lors d'un <CTRL-C> (énoncé étape 1.c).
void handlerSIGINT(int sig)
{
  (void) sig;
  fprintf(stderr,"\n(SERVEUR %d) Arret demande (CTRL-C), nettoyage des ressources...\n",getpid());
  msgctl(idQ,IPC_RMID,NULL);
  mysql_close(connexion);
  exit(0);
}