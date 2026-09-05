#include "windowclient.h"
#include "ui_windowclient.h"
#include <QMessageBox>
#include "dialogmodification.h"
#include <unistd.h>
#include <sys/types.h>                 // ETAPE 1a - AJOUTÉ : nécessaire pour msgget/msgsnd (envoi de CONNECT/DECONNECT)
#include <sys/ipc.h>                   // ETAPE 1a - AJOUTÉ
#include <sys/msg.h>                   // ETAPE 1a - AJOUTÉ
#include <signal.h>                    // ETAPE 1b - AJOUTÉ : nécessaire pour sigaction (réception de la réponse au LOGIN)

extern WindowClient *w;

#include "protocole.h"

int idQ, idShm;
bool loggedIn = false;    // ETAPE 1a/1b - AJOUTE : memorise si un utilisateur est loggé, pour savoir s'il faut encore envoyer LOGOUT avant DECONNECT 
                          // à la fermeture et pour activer/désactiver les boutons (1b)  
#define TIME_OUT 120
int timeOut = TIME_OUT;

void handlerSIGUSR1(int sig);
void handlerSIGALRM(int sig);   // ETAPE 3 - AJOUT

/* *** ETAPE 1a - AJOUTE ***
BUT : petite fonction utilitaire, factorise l'envoi des requêtes qui n'ont pas de données à transmettre (CONNECT, DECONNECT et aussi LOGOUT en 1b).
Envoie une requete "sans donnees" (CONNECT, DECONNECT ou LOGOUT) au serveur.
*/
static void envoiRequeteSimple(int requete)
{
  MESSAGE m;
  m.type = 1; // 1 = destination "Serveur" (voir protocole.h)
  m.expediteur = getpid();
  m.requete = requete;
  msgsnd(idQ, &m, sizeof(MESSAGE) - sizeof(long), 0);
}

/* *** ETAPE 2 - AJOUTE ***
Envoie une requete contenant un nom d'utilisateur dans data1 (ACCEPT_USER ou REFUSE_USER) au serveur.
*/
static void envoieRequeteAvecNom(int requete, const char* nom)
{
  MESSAGE m;
  m.type = 1;
  m.expediteur = getpid();
  m.requete = requete;
  strcpy(m.data1,nom);
  msgsnd(idQ,&m,sizeof(MESSAGE)-sizeof(long),0);
}

/* *** ETAPE 3 - AJOUT ***
BUT : a appeler a chaque action de l'utilisateur (bouton/checkbox) une fois logge,
pour lui redonner TIME_OUT secondes avant la prochaine deconnexion automatique.
*/
static void resetTimeOut()
{
  alarm(0);              // annule l'alarme en cours
  timeOut = TIME_OUT;    // on relance le compte a rebours a 120
  w->setTimeOut(timeOut);
  alarm(1);              // prochain tic dans 1 seconde
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
WindowClient::WindowClient(QWidget *parent):QMainWindow(parent),ui(new Ui::WindowClient)
{
    ui->setupUi(this);
    //::close(2); //ferme le descripteur de fichier 2, càd stderr ! --> empeche de voir les messages perror
    logoutOK();

    // Recuperation de l'identifiant de la file de messages
    fprintf(stderr,"(CLIENT %d) Recuperation de l'id de la file de messages\n",getpid());
        // *** ETAPE 1a - DEBUT MODIF ***
        /*
        BUT : "la fenêtre client envoie une requête CONNECT au serveur" (énoncé étape 1.a) — mais pour ça, 
        il faut d'abord récupérer l'identifiant de la file de messages déjà créée par le Serveur. 
        Avant : seul le fprintf existait, msgget n'était jamais appelé
        */
    if ((idQ = msgget(CLE,0)) == -1)
    {
      perror("(CLIENT) Erreur de msgget : le Serveur est-il lance ?");
      exit(1);
    }
        // *** ETAPE 1a - FIN MODIF ***

    // Recuperation de l'identifiant de la mémoire partagée
    fprintf(stderr,"(CLIENT %d) Recuperation de l'id de la mémoire partagée\n",getpid());

    // Attachement à la mémoire partagée

    // Armement des signaux
        // ***ÉTAPE 1b - DÉBUT AJOUT ***
        /* But : le serveur "envoie au processus client le signal SIGUSR1 pour le prévenir qu'il lui a envoyé un message" en réponse au LOGIN (énoncé étape 1.b) 
           il faut donc armer ce signal pour pouvoir le recevoir. 
         */
    struct sigaction sa;
    sa.sa_handler = handlerSIGUSR1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1,&sa,NULL);
        // ★★★ ÉTAPE 1b - FIN AJOUT

        // *** ETAPE 3 - AJOUT ***
        // But : armer SIGALRM pour pouvoir gerer le Time Out d'inactivite.
    struct sigaction saAlrm;
    saAlrm.sa_handler = handlerSIGALRM;
    sigemptyset(&saAlrm.sa_mask);
    saAlrm.sa_flags = 0;
    sigaction(SIGALRM,&saAlrm,NULL);
    
    // Envoi d'une requete de connexion au serveur
    envoiRequeteSimple(CONNECT);          // *** ÉTAPE 1a - AJOUTÉ : "avant même d'apparaître, elle envoie une requête CONNECT au serveur" (énoncé étape 1.a) ***
}

WindowClient::~WindowClient()
{
    delete ui;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions utiles : ne pas modifier /////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setNom(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditNom->clear();
    return;
  }
  ui->lineEditNom->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowClient::getNom()
{
  strcpy(connectes[0],ui->lineEditNom->text().toStdString().c_str());
  return connectes[0];
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setMotDePasse(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditMotDePasse->clear();
    return;
  }
  ui->lineEditMotDePasse->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowClient::getMotDePasse()
{
  strcpy(motDePasse,ui->lineEditMotDePasse->text().toStdString().c_str());
  return motDePasse;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
int WindowClient::isNouveauChecked()
{
  if (ui->checkBoxNouveau->isChecked()) return 1;
  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setPublicite(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditPublicite->clear();
    return;
  }
  ui->lineEditPublicite->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setTimeOut(int nb)
{
  ui->lcdNumberTimeOut->display(nb);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setAEnvoyer(const char* Text)
{
  //fprintf(stderr,"---%s---\n",Text);
  if (strlen(Text) == 0 )
  {
    ui->lineEditAEnvoyer->clear();
    return;
  }
  ui->lineEditAEnvoyer->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowClient::getAEnvoyer()
{
  strcpy(aEnvoyer,ui->lineEditAEnvoyer->text().toStdString().c_str());
  return aEnvoyer;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setPersonneConnectee(int i,const char* Text)
{
  if (strlen(Text) == 0 )
  {
    switch(i)
    {
        case 1 : ui->lineEditConnecte1->clear(); break;
        case 2 : ui->lineEditConnecte2->clear(); break;
        case 3 : ui->lineEditConnecte3->clear(); break;
        case 4 : ui->lineEditConnecte4->clear(); break;
        case 5 : ui->lineEditConnecte5->clear(); break;
    }
    return;
  }
  switch(i)
  {
      case 1 : ui->lineEditConnecte1->setText(Text); break;
      case 2 : ui->lineEditConnecte2->setText(Text); break;
      case 3 : ui->lineEditConnecte3->setText(Text); break;
      case 4 : ui->lineEditConnecte4->setText(Text); break;
      case 5 : ui->lineEditConnecte5->setText(Text); break;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowClient::getPersonneConnectee(int i)
{
  QLineEdit *tmp;
  switch(i)
  {
    case 1 : tmp = ui->lineEditConnecte1; break;
    case 2 : tmp = ui->lineEditConnecte2; break;
    case 3 : tmp = ui->lineEditConnecte3; break;
    case 4 : tmp = ui->lineEditConnecte4; break;
    case 5 : tmp = ui->lineEditConnecte5; break;
    default : return NULL;
  }

  strcpy(connectes[i],tmp->text().toStdString().c_str());
  return connectes[i];
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::ajouteMessage(const char* personne,const char* message)
{
  // Choix de la couleur en fonction de la position
  int i=1;
  bool trouve=false;
  while (i<=5 && !trouve)
  {
      if (getPersonneConnectee(i) != NULL && strcmp(getPersonneConnectee(i),personne) == 0) trouve = true;
      else i++;
  }
  char couleur[40];
  if (trouve)
  {
      switch(i)
      {
        case 1 : strcpy(couleur,"<font color=\"red\">"); break;
        case 2 : strcpy(couleur,"<font color=\"blue\">"); break;
        case 3 : strcpy(couleur,"<font color=\"green\">"); break;
        case 4 : strcpy(couleur,"<font color=\"darkcyan\">"); break;
        case 5 : strcpy(couleur,"<font color=\"orange\">"); break;
      }
  }
  else strcpy(couleur,"<font color=\"black\">");
  if (strcmp(getNom(),personne) == 0) strcpy(couleur,"<font color=\"purple\">");

  // ajout du message dans la conversation
  char buffer[300];
  sprintf(buffer,"%s(%s)</font> %s",couleur,personne,message);
  ui->textEditConversations->append(buffer);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setNomRenseignements(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditNomRenseignements->clear();
    return;
  }
  ui->lineEditNomRenseignements->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowClient::getNomRenseignements()
{
  strcpy(nomR,ui->lineEditNomRenseignements->text().toStdString().c_str());
  return nomR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setGsm(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditGsm->clear();
    return;
  }
  ui->lineEditGsm->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setEmail(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditEmail->clear();
    return;
  }
  ui->lineEditEmail->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::setCheckbox(int i,bool b)
{
  QCheckBox *tmp;
  switch(i)
  {
    case 1 : tmp = ui->checkBox1; break;
    case 2 : tmp = ui->checkBox2; break;
    case 3 : tmp = ui->checkBox3; break;
    case 4 : tmp = ui->checkBox4; break;
    case 5 : tmp = ui->checkBox5; break;
    default : return;
  }
  tmp->setChecked(b);
  if (b) tmp->setText("Accepté");
  else tmp->setText("Refusé");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::loginOK()
{
  ui->pushButtonLogin->setEnabled(false);
  ui->pushButtonLogout->setEnabled(true);
  ui->lineEditNom->setReadOnly(true);
  ui->lineEditMotDePasse->setReadOnly(true);
  ui->checkBoxNouveau->setEnabled(false);
  ui->pushButtonEnvoyer->setEnabled(true);
  ui->pushButtonConsulter->setEnabled(true);
  ui->pushButtonModifier->setEnabled(true);
  ui->checkBox1->setEnabled(true);
  ui->checkBox2->setEnabled(true);
  ui->checkBox3->setEnabled(true);
  ui->checkBox4->setEnabled(true);
  ui->checkBox5->setEnabled(true);
  ui->lineEditAEnvoyer->setEnabled(true);
  ui->lineEditNomRenseignements->setEnabled(true);
  setTimeOut(TIME_OUT);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::logoutOK()
{
  ui->pushButtonLogin->setEnabled(true);
  ui->pushButtonLogout->setEnabled(false);
  ui->lineEditNom->setReadOnly(false);
  ui->lineEditNom->setText("");
  ui->lineEditMotDePasse->setReadOnly(false);
  ui->lineEditMotDePasse->setText("");
  ui->checkBoxNouveau->setEnabled(true);
  ui->pushButtonEnvoyer->setEnabled(false);
  ui->pushButtonConsulter->setEnabled(false);
  ui->pushButtonModifier->setEnabled(false);
  for (int i=1 ; i<=5 ; i++)
  {
      setCheckbox(i,false);
      setPersonneConnectee(i,"");
  }
  ui->checkBox1->setEnabled(false);
  ui->checkBox2->setEnabled(false);
  ui->checkBox3->setEnabled(false);
  ui->checkBox4->setEnabled(false);
  ui->checkBox5->setEnabled(false);
  setNomRenseignements("");
  setGsm("");
  setEmail("");
  ui->textEditConversations->clear();
  setAEnvoyer("");
  ui->lineEditAEnvoyer->setEnabled(false);
  ui->lineEditNomRenseignements->setEnabled(false);
  setTimeOut(TIME_OUT);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions permettant d'afficher des boites de dialogue /////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::dialogueMessage(const char* titre,const char* message)
{
   QMessageBox::information(this,titre,message);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::dialogueErreur(const char* titre,const char* message)
{
   QMessageBox::critical(this,titre,message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Clic sur la croix de la fenêtre ////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::closeEvent(QCloseEvent *event)
{
    (void) event;

    // ***ÉTAPE 1b - AJOUTÉ ***
    // But : "si un utilisateur clique sur la croix de la fenêtre alors qu'il est loggé, une requête LOGOUT doit être envoyée au serveur avant
    // l'envoi de la requête DECONNECT" (énoncé étape 1.b).
    if (loggedIn)
    {
        envoiRequeteSimple(LOGOUT);
        loggedIn = false;
        alarm(0);   //ETAPE 3 - AJOUT : pour couper le compte à rebours puisque la session se termine volontairement
    }

    // ***  ÉTAPE 1a - AJOUTÉ ***
    // But : "un clic sur la croix de la fenêtre envoie une requête DECONNECT au serveur [...] puis termine le processus Client" (énoncé étape 1.a).
    // Avant : il y avait juste "// TO DO" puis QApplication::exit() direct, sans jamais prévenir le serveur.
    envoiRequeteSimple(DECONNECT);

    QApplication::exit();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions clics sur les boutons ////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::on_pushButtonLogin_clicked()
{
    // *** ÉTAPE 1b - DÉBUT AJOUT ***
    // But : "un clic sur le bouton Login envoie une requête LOGIN au
    // serveur, celle-ci contient le nom (data2), le mot de passe (texte) et
    // un entier précisant s'il s'agit d'un nouvel utilisateur ou pas
    // (data1)" (énoncé étape 1.b). Avant : juste "// TO DO", rien n'était envoyé.
    if (strlen(getNom()) == 0)
    {
        dialogueErreur("Login...","Veuillez encoder un nom d'utilisateur.");
        return;
    }

    MESSAGE m;
    m.type = 1;
    m.expediteur = getpid();
    m.requete = LOGIN;
    strcpy(m.data1,isNouveauChecked() ? "1" : "0");
    strcpy(m.data2,getNom());
    strcpy(m.texte,getMotDePasse());
    msgsnd(idQ,&m,sizeof(MESSAGE)-sizeof(long),0);
    // *** ÉTAPE 1b - FIN AJOUT ***
}

void WindowClient::on_pushButtonLogout_clicked()
{
    // *** ÉTAPE 1b - AJOUTÉ ***
    // But : "un clic sur le bouton Logout doit envoyer une requête LOGOUT au
    // serveur" (énoncé étape 1.b). Avant : seul logoutOK() était appelé, le
    // serveur n'était jamais prévenu.
    envoiRequeteSimple(LOGOUT);
    loggedIn = false;
    // *** ETAPE 1b - FIN AJOUT ***
    alarm(0);   // ETAPE 3 - AJOUT 

    logoutOK();
}

void WindowClient::on_pushButtonEnvoyer_clicked()
{
    resetTimeOut();   //ETAPE 3 - AJOUT
    // ETAPE 2.c : envoi du message au serveur, qui se chargera de le retransmettre aux utilisateurs que CE client a acceptes.
    if (strlen(getAEnvoyer()) == 0) return;

    MESSAGE m;
    m.type = 1;
    m.expediteur = getpid();
    m.requete = SEND;
    strcpy(m.texte,getAEnvoyer());
    msgsnd(idQ,&m,sizeof(MESSAGE)-sizeof(long),0);

    setAEnvoyer(""); // on vide le champ de saisie une fois le message envoye
}

void WindowClient::on_pushButtonConsulter_clicked()
{
    resetTimeOut();   //ETAPE 3 - AJOUT

}

void WindowClient::on_pushButtonModifier_clicked()
{
  // TO DO

  resetTimeOut();   //ETAPE 3 - AJOUT
  // Envoi d'une requete MODIF1 au serveur
  MESSAGE m;
  // ...

  // Attente d'une reponse en provenance de Modification
  fprintf(stderr,"(CLIENT %d) Attente reponse MODIF1\n",getpid());
  // ...

  // Verification si la modification est possible
  if (strcmp(m.data1,"KO") == 0 && strcmp(m.data2,"KO") == 0 && strcmp(m.texte,"KO") == 0)
  {
    QMessageBox::critical(w,"Problème...","Modification déjà en cours...");
    return;
  }

  // Modification des données par utilisateur
  DialogModification dialogue(this,getNom(),"",m.data2,m.texte);
  dialogue.exec();
  char motDePasse[40];
  char gsm[40];
  char email[40];
  strcpy(motDePasse,dialogue.getMotDePasse());
  strcpy(gsm,dialogue.getGsm());
  strcpy(email,dialogue.getEmail());

  // Envoi des données modifiées au serveur
  // ...
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions clics sur les checkbox ///////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowClient::on_checkBox1_clicked(bool checked)
{
    resetTimeOut();   //ETAPE 3 - AJOUT
    // ETAPE 2.b : le nom associe a la case 1 est celui affiche dans lineEditConnecte1
    const char* nom = getPersonneConnectee(1);
    if (strlen(nom) == 0) { ui->checkBox1->setChecked(!checked); return; }

    if (checked)
    {
        ui->checkBox1->setText("Accepté");
        /* *** ETAPE 2 - AJOUT *** */
        envoieRequeteAvecNom(ACCEPT_USER, nom);
    }
    else
    {
        ui->checkBox1->setText("Refusé");
        /* *** ETAPE 2 - AJOUT *** */
        envoieRequeteAvecNom(REFUSE_USER, nom);
    }
}

void WindowClient::on_checkBox2_clicked(bool checked)
{
    resetTimeOut();   //ETAPE 3 - AJOUT
    // ETAPE 2.b : le nom associe a la case 2 est celui affiche dans lineEditConnecte2
    const char* nom = getPersonneConnectee(2);
    if (strlen(nom) == 0) { ui->checkBox2->setChecked(!checked); return; }

    if (checked)
    {
        ui->checkBox2->setText("Accepté");
        /* *** ETAPE 2 - AJOUT *** */
        envoieRequeteAvecNom(ACCEPT_USER, nom);
    }
    else
    {
        ui->checkBox2->setText("Refusé");
        /* *** ETAPE 2 - AJOUT *** */
        envoieRequeteAvecNom(REFUSE_USER, nom);
    }
}

void WindowClient::on_checkBox3_clicked(bool checked)
{
    resetTimeOut();   //ETAPE 3 - AJOUT
    // ETAPE 2.b : le nom associe a la case 3 est celui affiche dans lineEditConnecte3
    const char* nom = getPersonneConnectee(3);
    if (strlen(nom) == 0) { ui->checkBox3->setChecked(!checked); return; }
    
    if (checked)
    {
        ui->checkBox3->setText("Accepté");
        /* *** ETAPE 2.b - AJOUT *** */
        envoieRequeteAvecNom(ACCEPT_USER, nom);
    }
    else
    {
        ui->checkBox3->setText("Refusé");
        /* ETAPE 2.b - AJOUT*/
        envoieRequeteAvecNom(REFUSE_USER, nom);
    }
}

void WindowClient::on_checkBox4_clicked(bool checked)
{
    resetTimeOut();   //ETAPE 3 - AJOUT
    // ETAPE 2.b : le nom associe a la case 4 est celui affiche dans lineEditConnecte2
    const char* nom = getPersonneConnectee(4);
    if (strlen(nom) == 0) { ui->checkBox4->setChecked(!checked); return; }
    
    if (checked)
    {
        ui->checkBox4->setText("Accepté");
        // ETAPE 2.b - AJOUT
        envoieRequeteAvecNom(ACCEPT_USER, nom);
    }
    else
    {
        ui->checkBox4->setText("Refusé");
        // ETAPE 2.b - AJOUT
        envoieRequeteAvecNom(REFUSE_USER, nom);
    }
}

void WindowClient::on_checkBox5_clicked(bool checked)
{
    resetTimeOut();   //ETAPE 3 - AJOUT
    // ETAPE 2.b : le nom associe a la case 5 est celui affiche dans lineEditConnecte2
    const char* nom = getPersonneConnectee(5);
    if (strlen(nom) == 0) { ui->checkBox5->setChecked(!checked); return; }
    
    if (checked)
    {
        ui->checkBox5->setText("Accepté");
        // ETAPE 2.b - AJOUT
        envoieRequeteAvecNom(ACCEPT_USER, nom);
    }
    else
    {
        ui->checkBox5->setText("Refusé");
        // ETAPE 2.b - AJOUT
        envoieRequeteAvecNom(REFUSE_USER, nom);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Handlers de signaux ////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handlerSIGUSR1(int sig)
{
    // *** ÉTAPE 1b - DÉBUT AJOUT ***
    // But : "le processus Client récupère les infos et les affiche dans la fenêtre" en lisant la réponse déposée par le serveur (énoncé étape 1.b). 
    // Avant : juste le commentaire "// ...msgrcv(idQ,&m,...)", rien n'était réellement lu.
    
    (void) sig;
    MESSAGE m;

    // *** ÉTAPE 2 - MODIFIÉ *** (remplace l'ancien "if" unique de l'Étape 1b)

    /* But : SIGUSR1 n'est pas mis en file par le noyau. Si le Serveur nous envoie plusieurs messages coup sur coup (ex : plusieurs ADD_USER lors d'un login), 
    les signaux peuvent se fusionner en un seul réveil. Il faut donc vider la file de TOUT ce qui nous est destiné à chaque réveil, pas lire un seul message. 
    La boucle s'arrête quand msgrcv renvoie -1 avec errno == ENOMSG (plus rien en attente) 
    — ce n'est pas une erreur, donc pas de perror ici.*/
    while (msgrcv(idQ,&m,sizeof(MESSAGE)-sizeof(long),getpid(),IPC_NOWAIT) != -1)
    {
      switch(m.requete)
      {
        case LOGIN :
                    if (strcmp(m.data1,"OK") == 0)
                    {
                      fprintf(stderr,"(CLIENT %d) Login OK\n",getpid());
                      loggedIn = true;  // *** ETAE 1b - AJOUTE : memorise que le login a reussi ! pour closeEvent (1a) et Logout (1b) ***
                      w->loginOK();
                      timeOut = TIME_OUT;    //ETAPE 3 - AJOUT
                      alarm(1);   //ETAPE 3 - AJOUT : 
                      w->dialogueMessage("Login...",m.texte);
                      // ...
                    }
                    else w->dialogueErreur("Login...",m.texte);
                    break;

        case ADD_USER :
                    // ETAPE 2.a : on cherche la premiere case libre (1..5) pour y afficher le nouvel utilisateur connu.
                    for(int i=1 ; i<=5 ; i++)
                    {
                      if(strlen(w->getPersonneConnectee(i)) == 0)
                      {
                        w->setPersonneConnectee(i, m.data1);
                        break;
                      }
                    }
                    break;

        case REMOVE_USER :
                      // ETAPE 2.a : on cherche la case qui contenait cet utilisateur pour la vider, et on remet le checkbox
                      // correspondant a "Refuse" (l'ancien reglage n'a plus de sens pour un emplacement desormais vide).
                      for (int i=1 ; i<=5 ; i++)
                        if (strcmp(w->getPersonneConnectee(i),m.data1) == 0)
                        {
                          w->setPersonneConnectee(i,"");
                          w->setCheckbox(i,false);
                          break;
                        }
                    break;

        case SEND :
                    // ETAPE 2.c : affichage du message recu (m.data1 = nom de l'expediteur, m.texte = contenu du message).
                    w->ajouteMessage(m.data1,m.texte);
                    break;

        case CONSULT :
                  // TO DO
                  break;
      }// FIN switch()
    }// FIN while
}
/* *** ETAPE 3 - AJOUT ***
BUT : gerer le Time Out d'inactivite. Appele chaque seconde tant que l'utilisateur
est logge et n'a rien clique. Si le compteur arrive a 0, on force le logout.
*/
void handlerSIGALRM(int sig)
{
    (void) sig;
    timeOut--;
    w->setTimeOut(timeOut);
    if (timeOut <= 0)
    {
        envoiRequeteSimple(LOGOUT);
        loggedIn = false;
        w->logoutOK();
    }
    else
    {
        alarm(1);
    }
}