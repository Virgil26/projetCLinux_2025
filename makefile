# ===============================================================================
# Makefile - Projet Final C/LINUX 2025 : "Le chat sélectif"
# Compile tous les exécutables : Administrateur, Client, Serveur,
# CreationBD, BidonFichierPub, Publicite, Consultation, Modification.
# Les chemins Qt5 / MySQL reprennent ceux de Compile.sh fourni par l'énoncé.
# ===============================================================================

CXX      = g++

CXXFLAGS = -pipe -g -std=gnu++11 -Wall -W -D_REENTRANT -fPIC \
           -DQT_DEPRECATED_WARNINGS -DQT_QML_DEBUG \
           -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB

QTINC    = -isystem /usr/include/qt5 \
           -isystem /usr/include/qt5/QtWidgets \
           -isystem /usr/include/qt5/QtGui \
           -isystem /usr/include/qt5/QtCore \
           -I/usr/lib64/qt5/mkspecs/linux-g++

QTLIBS   = /usr/lib64/libQt5Widgets.so /usr/lib64/libQt5Gui.so /usr/lib64/libQt5Core.so /usr/lib64/libGL.so -lpthread

MYSQLINC = -I/usr/include/mysql
MYSQLLIB = -m64 -L/usr/lib64/mysql -lmysqlclient -lpthread -lz -lm -lrt -lssl -lcrypto -ldl

# --------------------------------------------------------------------------
# Cible par défaut : compile tout
# --------------------------------------------------------------------------
all: Administrateur Client Serveur CreationBD BidonFichierPub Publicite Consultation Modification

# --------------------------------------------------------------------------
# Administrateur (application Qt)
# --------------------------------------------------------------------------
ADMIN_OBJS = mainAdmin.o windowadmin.o moc_windowadmin.o

Administrateur: $(ADMIN_OBJS)
	$(CXX) -o $@ $(ADMIN_OBJS) $(QTLIBS)

# --------------------------------------------------------------------------
# Client (application Qt)
# --------------------------------------------------------------------------
CLIENT_OBJS = mainClient.o windowclient.o dialogmodification.o moc_windowclient.o moc_dialogmodification.o

Client: $(CLIENT_OBJS)
	$(CXX) -o $@ $(CLIENT_OBJS) $(QTLIBS)

# Règle générique de compilation des .cpp Qt en .o
%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(QTINC) -I. -o $@ $<

# --------------------------------------------------------------------------
# Serveur, CreationBD, Consultation, Modification (utilisent MySQL)
# --------------------------------------------------------------------------
Serveur: Serveur.cpp FichierUtilisateur.cpp FichierUtilisateur.h protocole.h
	$(CXX) Serveur.cpp FichierUtilisateur.cpp -o Serveur $(MYSQLINC) $(MYSQLLIB)

CreationBD: CreationBD.cpp
	$(CXX) CreationBD.cpp -o CreationBD $(MYSQLINC) $(MYSQLLIB)

Consultation: Consultation.cpp protocole.h
	$(CXX) Consultation.cpp -o Consultation $(MYSQLINC) $(MYSQLLIB)

Modification: Modification.cpp FichierUtilisateur.cpp FichierUtilisateur.h protocole.h
	$(CXX) Modification.cpp FichierUtilisateur.cpp -o Modification $(MYSQLINC) $(MYSQLLIB)

# --------------------------------------------------------------------------
# BidonFichierPub, Publicite : aucune dépendance externe
# --------------------------------------------------------------------------
BidonFichierPub: BidonFichierPub.cpp protocole.h
	$(CXX) -o BidonFichierPub BidonFichierPub.cpp

Publicite: Publicite.cpp protocole.h
	$(CXX) -o Publicite Publicite.cpp

# --------------------------------------------------------------------------
# Nettoyage
# --------------------------------------------------------------------------
clean:
	rm -f *.o

mrproper: clean
	rm -f Administrateur Client Serveur CreationBD BidonFichierPub Publicite Consultation Modification

.PHONY: all clean mrproper
