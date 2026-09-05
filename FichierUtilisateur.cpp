/* ===========================================================================
 FichierUtilisateur.cpp

 Gère le fichier binaire "utilisateurs.dat" contenant, pour chaque
 utilisateur, son nom et le hash de son mot de passe (voir Exercice 2).
 Uniquement des appels systèmes bas niveau : open, read, write, lseek, close.

 Ce module n'est utilisé QUE par le Serveur et par le processus Modification
 (le Client et l'Administrateur ne touchent jamais directement ce fichier).
 ===========================================================================*/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include "FichierUtilisateur.h"

// ---------------------------------------------------------------------------
// Fonctions de l'exercice 2
// ---------------------------------------------------------------------------

int estPresent(const char* nom)
{
  int fd = open(FICHIER_UTILISATEURS, O_RDONLY);
  if (fd == -1) return -1; // le fichier n'existe pas encore

  UTILISATEUR u;
  int position = 0;
  int trouve = 0;

  while (!trouve && read(fd,&u,sizeof(UTILISATEUR)) == sizeof(UTILISATEUR))
  {
    position++;
    if (strcmp(u.nom,nom) == 0) trouve = 1;
  }

  close(fd);
  return trouve ? position : 0;
}

int hash(const char* motDePasse)
{
  int somme = 0;
  for (int i = 0 ; motDePasse[i] != '\0' ; i++)
    somme += (i+1) * (unsigned char) motDePasse[i];
  return somme % 97;
}

void ajouteUtilisateur(const char* nom, int hashMotDePasse)
{
  int fd = open(FICHIER_UTILISATEURS, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd == -1)
  {
    perror("(FichierUtilisateur) Erreur de open (ajouteUtilisateur)");
    return;
  }

  UTILISATEUR u;
  strncpy(u.nom,nom,sizeof(u.nom)-1);
  u.nom[sizeof(u.nom)-1] = '\0';
  u.hash = hashMotDePasse;

  if (write(fd,&u,sizeof(UTILISATEUR)) != sizeof(UTILISATEUR))
    perror("(FichierUtilisateur) Erreur de write (ajouteUtilisateur)");

  close(fd);
}

int verifieMotDePasse(int position, int hashMotDePasse)
{
  int fd = open(FICHIER_UTILISATEURS, O_RDONLY);
  if (fd == -1) return -1;

  lseek(fd,(long)(position-1) * sizeof(UTILISATEUR),SEEK_SET);

  UTILISATEUR u;
  if (read(fd,&u,sizeof(UTILISATEUR)) != sizeof(UTILISATEUR))
  {
    close(fd);
    return 0;
  }

  close(fd);
  return (u.hash == hashMotDePasse) ? 1 : 0;
}

int listeUtilisateurs(UTILISATEUR* utilisateurs)
{
  int fd = open(FICHIER_UTILISATEURS, O_RDONLY);
  if (fd == -1) return 0;

  int n = 0;
  while (read(fd,&utilisateurs[n],sizeof(UTILISATEUR)) == sizeof(UTILISATEUR)) n++;

  close(fd);
  return n;
}

// ---------------------------------------------------------------------------
// Fonctions ajoutées pour le Projet Final
// ---------------------------------------------------------------------------

int modifieMotDePasse(int position, int nouveauHashMotDePasse)
{
  int fd = open(FICHIER_UTILISATEURS, O_WRONLY);
  if (fd == -1) return 0;

  // On se positionne exactement sur le champ "hash" de l'enregistrement
  // (on ne touche pas au nom) grâce à offsetof.
  long offset = (long)(position-1) * sizeof(UTILISATEUR) + offsetof(UTILISATEUR,hash);
  lseek(fd,offset,SEEK_SET);
  write(fd,&nouveauHashMotDePasse,sizeof(int));

  close(fd);
  return 1;
}

int supprimeUtilisateur(const char* nom)
{
  if (estPresent(nom) <= 0) return 0; // -1 (pas de fichier) ou 0 (pas trouvé)

  int fdLecture = open(FICHIER_UTILISATEURS, O_RDONLY);
  if (fdLecture == -1) return 0;

  const char* nomTemp = "utilisateurs.dat.tmp";
  int fdEcriture = open(nomTemp, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  if (fdEcriture == -1)
  {
    perror("(FichierUtilisateur) Erreur de open (supprimeUtilisateur)");
    close(fdLecture);
    return 0;
  }

  UTILISATEUR u;
  while (read(fdLecture,&u,sizeof(UTILISATEUR)) == sizeof(UTILISATEUR))
    if (strcmp(u.nom,nom) != 0)
      write(fdEcriture,&u,sizeof(UTILISATEUR));

  close(fdLecture);
  close(fdEcriture);

  unlink(FICHIER_UTILISATEURS);
  rename(nomTemp,FICHIER_UTILISATEURS);

  return 1;
}