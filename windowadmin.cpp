#include "windowadmin.h"
#include "ui_windowadmin.h"
#include <QMessageBox>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

extern int idQ;

// *** ETAPE 6 - AJOUT ***
// Envoie une requete a destination du Serveur (type=1) en tant qu'Administrateur.
static void envoyerRequete(MESSAGE &m)
{
  m.type = 1;
  m.expediteur = getpid();
  msgsnd(idQ,&m,sizeof(MESSAGE)-sizeof(long),0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
WindowAdmin::WindowAdmin(QWidget *parent):QMainWindow(parent),ui(new Ui::WindowAdmin)
{
    ui->setupUi(this);
    ::close(2);
}

WindowAdmin::~WindowAdmin()
{
    delete ui;
}

// *** ETAPE 6 - AJOUT ***
// But : que la fenetre soit fermee via le bouton "Quitter" OU via la croix,
// le Serveur doit etre prevenu (LOGOUT_ADMIN) pour remettre pidAdmin a 0 -
// sinon plus aucun Administrateur ne peut se reconnecter par la suite.
void WindowAdmin::closeEvent(QCloseEvent *event)
{
    (void) event;
    MESSAGE m;
    m.requete = LOGOUT_ADMIN;
    envoyerRequete(m);
    exit(0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions utiles : ne pas modifier /////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::setNom(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditNom->clear();
    return;
  }
  ui->lineEditNom->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowAdmin::getNom()
{
  strcpy(nom,ui->lineEditNom->text().toStdString().c_str());
  return nom;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::setMotDePasse(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditMotDePasse->clear();
    return;
  }
  ui->lineEditMotDePasse->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowAdmin::getMotDePasse()
{
  strcpy(motDePasse,ui->lineEditMotDePasse->text().toStdString().c_str());
  return motDePasse;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::setTexte(const char* Text)
{
  if (strlen(Text) == 0 )
  {
    ui->lineEditTexte->clear();
    return;
  }
  ui->lineEditTexte->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* WindowAdmin::getTexte()
{
  strcpy(texte,ui->lineEditTexte->text().toStdString().c_str());
  return texte;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::setNbSecondes(int n)
{
  char Text[10];
  sprintf(Text,"%d",n);
  ui->lineEditNbSecondes->setText(Text);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
int WindowAdmin::getNbSecondes()
{
  char tmp[10];
  strcpy(tmp,ui->lineEditNbSecondes->text().toStdString().c_str());
  return atoi(tmp);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions permettant d'afficher des boites de dialogue /////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::dialogueMessage(const char* titre,const char* message)
{
   QMessageBox::information(this,titre,message);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::dialogueErreur(const char* titre,const char* message)
{
   QMessageBox::critical(this,titre,message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///// Fonctions clics sur les boutons ////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WindowAdmin::on_pushButtonAjouterUtilisateur_clicked()
{
  // *** ETAPE 6 - AJOUT ***
  if (strlen(getNom()) == 0)
  {
    dialogueErreur("Ajout utilisateur...","Veuillez encoder un nom d'utilisateur.");
    return;
  }
  MESSAGE m;
  m.requete = NEW_USER;
  strcpy(m.data1,getNom());
  strcpy(m.data2,getMotDePasse());
  envoyerRequete(m);

  if (msgrcv(idQ,&m,sizeof(MESSAGE)-sizeof(long),getpid(),0) == -1)
  {
    perror("(ADMINISTRATEUR) Erreur de msgrcv (NEW_USER)");
    return;
  }

  if (strcmp(m.data1,"OK") == 0)
    dialogueMessage("Ajout utilisateur...",m.texte);
  else
    dialogueErreur("Ajout utilisateur...",m.texte);
}

void WindowAdmin::on_pushButtonSupprimerUtilisateur_clicked()
{
  // *** ETAPE 6 - AJOUT ***
  if (strlen(getNom()) == 0)
  {
    dialogueErreur("Suppression utilisateur...","Veuillez encoder un nom d'utilisateur.");
    return;
  }
  MESSAGE m;
  m.requete = DELETE_USER;
  strcpy(m.data1,getNom());
  envoyerRequete(m);

  if (msgrcv(idQ,&m,sizeof(MESSAGE)-sizeof(long),getpid(),0) == -1)
  {
    perror("(ADMINISTRATEUR) Erreur de msgrcv (DELETE_USER)");
    return;
  }

  if (strcmp(m.data1,"OK") == 0)
    dialogueMessage("Suppression utilisateur...",m.texte);
  else
    dialogueErreur("Suppression utilisateur...",m.texte);
}

void WindowAdmin::on_pushButtonAjouterPublicite_clicked()
{
  // *** ETAPE 6 - AJOUT ***
  if (strlen(getTexte()) == 0)
  {
    dialogueErreur("Ajout publicite...","Veuillez encoder le texte de la publicite.");
    return;
  }
  int nb = getNbSecondes();
  if (nb <= 0)
  {
    dialogueErreur("Ajout publicite...","Veuillez encoder un nombre de secondes valide.");
    return;
  }
  MESSAGE m;
  m.requete = NEW_PUB;
  sprintf(m.data1,"%d",nb);
  strcpy(m.texte,getTexte());
  envoyerRequete(m);
  // Pas de reponse attendue ici : voir protocole.h, NEW_PUB n'a pas de sens S -> A.
}

void WindowAdmin::on_pushButtonQuitter_clicked()
{
  // *** ETAPE 6 - AJOUT ***
  // Le nettoyage (LOGOUT_ADMIN) est factorise dans closeEvent(), declenche par close() 
  // - ainsi, quitter via ce bouton ou via la croix a le meme effet.
  this->close();
}
