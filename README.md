# 📌 Cahier des charges  
## 🎒 Projet : Antivol connecté LoRa

---

## 1. 🎯 Objectif du projet

Réaliser un système antivol connecté permettant de surveiller un objet (sac, valise, vélo, etc.).  
Le système doit être capable de détecter un mouvement anormal et de déclencher une alerte locale ainsi qu’une transmission d’information à distance via une communication radio (LoRa 868 MHz).

---

## 2. ❓ Problématique

Comment concevoir un système autonome capable de :
- détecter le déplacement d’un objet
- alerter immédiatement l’utilisateur
- transmettre l’information à distance de manière fiable

---

## 3. ⚙️ Fonctionnalités attendues

### Fonction principale
- Détection de mouvement à l’aide d’un accéléromètre

### Fonctions secondaires
- Activation / désactivation du système
- Alerte locale (LED et/ou buzzer)
- Envoi d’une alerte via LoRa en cas de mouvement

### Fonction bonus (optionnelle)
- Activation sécurisée via clavier (code utilisateur)

---

## 4. 🧠 Principe de fonctionnement

1. L’utilisateur active le système
2. Le système enregistre un état de référence (immobile)
3. L’accéléromètre surveille les variations de mouvement
4. Si un seuil est dépassé :
   - Déclenchement d’une alerte locale (LED / buzzer)
   - Envoi d’un message via LoRa
5. Le système reste actif jusqu’à désactivation

---

## 5. 🧰 Matériel utilisé

- Carte microcontrôleur (UCA)
- Accéléromètre (KXTJ3)
- Module LoRa 868 MHz
- LED
- Buzzer
- Bouton poussoir (activation/désactivation)

### Matériel optionnel
- Clavier numérique (pad)
- Écran OLED (affichage état)

---

## 6. 🔌 Contraintes techniques

- Utilisation obligatoire d’une communication radio LoRa 868 MHz
- Système embarqué autonome
- Temps de développement limité
- Utilisation du matériel disponible au laboratoire
- Fiabilité du système prioritaire

---

## 7. 🗂️ Organisation du projet

### Étapes de réalisation

1. Mise en place du capteur (accéléromètre)
2. Détection de mouvement (seuil)
3. Mise en place de l’alerte locale (LED / buzzer)
4. Intégration de la communication LoRa
5. Ajout du système d’activation/désactivation
6. Tests et validation
7. Ajout éventuel du clavier (bonus)

---

## 8. 📡 Architecture du système

- Entrée : accéléromètre
- Traitement : microcontrôleur
- Sorties :
  - LED / buzzer
  - transmission LoRa

---

## 9. 🧪 Critères de validation

- Détection correcte d’un mouvement réel
- Absence de faux déclenchements excessifs
- Transmission fonctionnelle via LoRa
- Réactivité du système
- Fiabilité globale

---

## 10. 📊 Livrables attendus

- Code source (GitHub)
- Schémas du montage
- Présentation orale
- Démonstration fonctionnelle

---

## 11. 🚀 Perspectives d’évolution

- Ajout d’un système de géolocalisation
- Application mobile de réception
- Amélioration de la sécurité (code, authentification)
- Optimisation énergétique

---
