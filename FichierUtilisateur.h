#ifndef FICHIERUTILISATEUR_H
#define FICHIERUTILISATEUR_H

// Nom du fichier binaire contenant les utilisateurs (nom + hash du mot de passe)
#define FICHIER_UTILISATEURS "utilisateurs.dat"

// Un enregistrement du fichier utilisateurs.dat (identique à l'exercice 2)
typedef struct
{
  char nom[20];
  int  hash;
} UTILISATEUR;

// ---------------------------------------------------------------------------
// Fonctions de l'exercice 2 (interface imposée, reprise telle quelle)
// ---------------------------------------------------------------------------

// Tente d'ouvrir le fichier en lecture seule.
// Si le fichier n'existe pas : retourne -1.
// Sinon, lit séquentiellement le fichier structure par structure jusqu'à
// trouver l'utilisateur "nom". Si trouvé : retourne sa POSITION dans le
// fichier (1,2,3,... et PAS l'indice). 
// Si non trouvé : retourne 0.
int estPresent(const char* nom);

// Retourne le hash d'un mot de passe : somme (modulo 97) pondérée (par leur
// position, en partant de 1) des codes ASCII des caractères du mot de passe.
int hash(const char* motDePasse);

// Ouvre le fichier en écriture seule + écriture en fin de fichier (le crée
// s'il n'existe pas encore) et y enregistre un nouvel utilisateur (nom + hash
// du mot de passe, déjà calculé par la fonction hash()).
void ajouteUtilisateur(const char* nom, int hashMotDePasse);

// Ouvre le fichier en lecture seule (retourne -1 si le fichier n'existe pas).
// Lit la structure UTILISATEUR à la "position" reçue en paramètre (celle
// renvoyée par estPresent) et compare son hash à hashMotDePasse.
// Retourne 1 si identiques (mot de passe correct), 0 sinon.
int verifieMotDePasse(int position, int hashMotDePasse);

// Lit tous les utilisateurs du fichier dans le vecteur fourni par l'appelant
// (qui doit être suffisamment grand). Retourne le nombre d'utilisateurs lus.
int listeUtilisateurs(UTILISATEUR* utilisateurs);

// ---------------------------------------------------------------------------
// Fonctions ajoutées pour les besoins du Projet Final (étapes 5 et 6 : un
// utilisateur peut changer son mot de passe, l'administrateur peut supprimer
// un utilisateur). L'énoncé du projet final demande explicitement de
// "compléter" le module FichierUtilisateur en conséquence.
// ---------------------------------------------------------------------------

// Modifie le hash du mot de passe de l'utilisateur enregistré à "position"
// (position obtenue via estPresent). Retourne 1 si la modification a réussi,
// 0 si le fichier n'existe pas.
int modifieMotDePasse(int position, int nouveauHashMotDePasse);

// Supprime l'utilisateur "nom" du fichier (réécrit le fichier sans lui).
// Retourne 1 si l'utilisateur a été supprimé, 0 s'il n'existait pas.
int supprimeUtilisateur(const char* nom);

#endif // FICHIERUTILISATEUR_H