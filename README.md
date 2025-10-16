# CAN Socket Collector

Collecteur CAN multithread en C++17 pour déploiement sur OWA4X. Ce programme lit les trames CAN via socket réseau, les décode avec DBC, et les sauvegarde au format MF4 avec rotation automatique.

## Architecture

```
CAN Bus → CanReader → Queue → DbcDecoder → Queue → Mf4Writer → MF4 Files
```

- **CanReader**: Lecture des trames CAN sur interface can1
- **DbcDecoder**: Décodage des signaux avec dbcppp
- **Mf4Writer**: Écriture MF4 avec rotation à 15 Mo

## Compilation

### Cross-compilation pour OWA4X (ARM)
```bash
# Build release
make all

# Build debug avec symboles
make debug

# Voir la configuration
make info

# Nettoyer
make clean
```

### Installation sur OWA4X
```bash
# Définir l'hôte cible
export OWA_HOST=192.168.1.100

# Installer sur le device
make install
```

## Utilisation

```bash
# Utilisation basique
./can_socket_collector --dbc signals.dbc --output-dir /tmp/mf4_data

# Spécifier l'interface CAN
./can_socket_collector --dbc signals.dbc --output-dir /tmp/mf4_data --interface can1

# Aide
./can_socket_collector --help
```

## Fonctionnalités

- **Lecture CAN**: Socket CAN non-bloquant sur interface can1
- **Décodage DBC**: Support complet des signaux DBC avec dbcppp
- **Format MF4**: Écriture avec mdflib et rotation automatique à 15 Mo
- **Nommage horodaté**: Fichiers au format `can_data_YYYYMMDD_HHMMSS.mf4`
- **Arrêt gracieux**: Gestion des signaux SIGINT/SIGTERM sans perte de données
- **Threading**: Pipeline multithread avec queues thread-safe

## Structure des fichiers

```
├── src/
│   ├── main.cpp              # Point d'entrée et coordination
│   ├── can_reader.cpp        # Lecture socket CAN
│   ├── dbc_decoder.cpp       # Décodage DBC
│   ├── mf4_writer.cpp        # Écriture MF4
│   └── signal_handler.cpp    # Gestion signaux système
├── include/
│   ├── thread_safe_queue.h   # Queue thread-safe template
│   ├── can_frame.h           # Structures données CAN
│   ├── can_reader.h          # Interface CanReader
│   ├── dbc_decoder.h         # Interface DbcDecoder
│   ├── mf4_writer.h          # Interface Mf4Writer
│   └── signal_handler.h      # Interface SignalHandler
└── Makefile                  # Configuration build cross-compile
```

## Dépendances

- **dbcppp**: Décodage DBC (pré-installé sur OWA4X)
- **mdflib**: Format MF4 (pré-installé sur OWA4X)
- **Threads**: std::thread, std::mutex, std::condition_variable
- **Socket CAN**: Support Linux kernel

## Déploiement OWA4X

Le binaire cross-compilé et les fichiers JSON sont automatiquement copiés vers `/home/seloni/acq_json/` sur le device OWA4X.
