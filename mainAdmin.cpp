#include "windowadmin.h"
#include <QApplication>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
// *** ETAPE 6 - AJOUT ***
#include <string.h>
#include <QMessageBox>

int idQ;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Recuperation de l'identifiant de la file de messages
    fprintf(stderr,"(ADMINISTRATEUR %d) Recuperation de l'id de la file de messages\n",getpid());

    // *** ETAPE 6 - AJOUT ***
    if ((idQ = msgget(CLE,0)) == -1)
    {
        perror("(ADMINISTRATEUR) Erreur de msgget : le Serveur est-il lance ?");
        exit(1);
    }

    // Envoi d'une requete de connexion au serveur
    MESSAGE m;
    // *** ETAPE 6 - AJOUT ***
    m.type = 1;
    m.expediteur = getpid();
    m.requete = LOGIN_ADMIN;
    msgsnd(idQ,&m,sizeof(MESSAGE)-sizeof(long),0);

    // Attente de la réponse
    fprintf(stderr,"(ADMINISTRATEUR %d) Attente reponse\n",getpid());

    // *** ETAPE 6 - AJOUT ***
    if (msgrcv(idQ,&m,sizeof(MESSAGE)-sizeof(long),getpid(),0) == -1)
    {
        perror("(ADMINISTRATEUR) Erreur de msgrcv (LOGIN_ADMIN)");
        exit(1);
    }

    if (strcmp(m.data1,"KO") == 0)
    {
        QMessageBox::critical(nullptr,"Administrateur","Un administrateur est deja connecte.");
        return 1;
    }

    
    WindowAdmin w;
    w.show();
    return a.exec();
}
