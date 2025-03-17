# Chat Client-Serveur en C

💬 **Un système de messagerie instantanée basé sur TCP/IP avec support des salons et transfert de fichiers.**

---

## 📋 Fonctionnalités
✔️ Communication en temps réel entre clients via un serveur central.
✔️ Gestion des utilisateurs avec pseudonymes uniques.
✔️ Création et gestion de salons de discussion.
✔️ Envoi de messages privés et publics.
✔️ Transfert de fichiers en mode pair-à-pair.
✔️ Support multi-clients avec `poll()`.
✔️ Vérification et gestion des erreurs réseau.

---

## 🔧 Prérequis
📌 **Système** : Linux.
📌 **Compilateur** : GCC
📌 **Outils** : `make`, `valgrind`

---

## 📦 Installation
```sh
# Cloner le dépôt
git clone https://github.com/abdelaaziz0/CHAT_CLIENT_SERVEUR.git

# Compiler le projet
make
```

---

## 🚀 Utilisation
### Lancer le serveur
```sh
./server <port>
```

### Lancer un client
```sh
./client <server_name> <server_port>
```

---

## 📝 Commandes disponibles
### 📌 Gestion des utilisateurs
- `/nick <pseudo>` : Définir ou changer de pseudo.
- `/who` : Voir la liste des utilisateurs connectés.
- `/whois <pseudo>` : Obtenir des infos sur un utilisateur.

### 📌 Messages
- `/msg <pseudo> <message>` : Envoyer un message privé.
- `/msgall <message>` : Envoyer un message à tous.

### 📌 Salons
- `/create <nom_salon>` : Créer un salon.
- `/channel_list` : Lister les salons existants.
- `/join <nom_salon>` : Rejoindre un salon.
- `/quit <nom_salon>` : Quitter un salon.

### 📌 Transfert de fichiers
- `/send <pseudo> <fichier>` : Envoyer un fichier à un utilisateur.

---

## 🔍 Débogage et Outils utiles
🛠 **Détection des fuites mémoire**
```sh
valgrind ./server <port>
```

🛠 **Test de connexion sans client**
```sh
telnet localhost <port>
```

🛠 **Visualiser les sockets ouvertes**
```sh
lsof -c ./server | grep TCP
```

---

## 📂 Structure du projet
```
.
├── client.c          # Code source du client
├── server.c          # Code source du serveur
├── msg_struct.h      # Définition des structures de messages
├── common.h          # Constantes et configurations
├── Makefile          # Compilation automatisée
├── README.md         # Documentation du projet
```

---

## 📄 Licence
Ce projet est sous licence MIT. Voir le fichier `LICENSE` pour plus de détails.

