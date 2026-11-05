# Temperature centrale termica

Per installare il broker sul server Docker seguire la [guida Docker Compose](deploy/mosquitto/README.md).

Firmware ESP-IDF 6.1 per ESP32-WROOM (`esp32`), ESP32-C3 ed ESP32-C6.
Fino a 8 DS18B20 su un unico bus 1-Wire; letture ogni 15 secondi,
invio MQTT e discovery Home Assistant. Nessun comando su caldaia o pompe.

## Preparazione

Aprire questa cartella in VS Code con l'estensione Espressif.
Nel terminale ESP-IDF, dopo aver scelto la scheda:

```powershell
idf.py set-target esp32c6
idf.py menuconfig
```

Per WROOM usare `esp32`, per C3 usare `esp32c3`.
Nel menu **Centrale termica** impostare SSID e password Wi-Fi, URI del broker
MQTT (esempio `mqtt://192.168.1.10:1883`), utente/password e GPIO dati.
SSID vuoto disabilita la rete e permette di leggere le sonde dal monitor seriale.
Il GPIO 4 e l'indirizzo del broker sono segnaposto da verificare prima del collegamento.
Non usare pin occupati da flash, USB, console o periferiche della specifica scheda.

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Sostituire COM5 con la porta effettiva. Uscire dal monitor con Ctrl+].
Non caricare il firmware di un target su un altro modello.
Le credenziali finiscono in sdkconfig e nel firmware: i file locali di configurazione
sono esclusi da Git. Il trasporto previsto è MQTT sulla rete locale, senza TLS.

## Collegamenti

Usare sonde a tre fili: VDD a 3,3 V, GND comune e DQ al GPIO scelto.
Inserire una resistenza esterna da 4,7 kohm tra DQ e 3,3 V.
Tutte le sonde condividono alimentazione e linea dati. La modalità parassita
non è supportata. Verificare il pinout reale delle sonde, senza affidarsi ai colori.
Lunghezze e disposizione dei cavi saranno da valutare sull'impianto;
provare prima le sonde con collegamenti corti.

## Home Assistant

Occorrono un broker (per esempio Mosquitto) e l'integrazione MQTT configurata
in Home Assistant, con il prefisso discovery predefinito `homeassistant`.
Il dispositivo compare come `centrale_<MAC>`, con un sensore per ogni ROM DS18B20.
Rinominare i sensori in Home Assistant (mandata, ritorno, accumulo, ecc.):
l'identità dipende dalla ROM, non dall'ordine di rilevamento.

Topic:

- `homeassistant/sensor/<nodo>_<ROM>/config`: discovery retained.
- `centrale/<nodo>/availability`: online/offline con last will retained.
- `centrale/<nodo>/<ROM>/temperature`: gradi Celsius, QoS 1, senza retain.
- `centrale/<nodo>/<ROM>/availability`: stato della singola sonda.

La ricerca viene ripetuta ogni ciclo per rilevare sonde aggiunte dopo l'avvio.
Le sonde scollegate restano registrate e diventano non disponibili; quando
ritornano riprendono a pubblicare. Massimo 8 ROM distinte per avvio: dopo
la sostituzione di una sonda a capacità piena, riavviare il dispositivo.
Le vecchie entità di sonde sostituite si rimuovono manualmente da Home Assistant
e cancellando il relativo topic discovery retained dal broker.
Letture fallite o fuori intervallo non vengono inviate come temperature.
Home Assistant considera scaduto il valore dopo tre intervalli senza aggiornamenti.
La discovery viene ripubblicata ogni ciclo per gestire anche il riavvio di Home Assistant.

## Verifiche sul banco

Compilazione verificata con ESP-IDF 6.1 per `esp32`, `esp32c3` e `esp32c6`,
inclusa generazione dei binari e controllo dello spazio nella partizione app.
Non ancora provato su scheda, sonde e broker reali.

Prima dell'installazione verificare lettura delle 8 sonde, nomi dopo riavvio,
scollegamento e riconnessione di una sonda, perdita Wi-Fi, riavvio broker e Home Assistant.
La compilazione non sostituisce queste verifiche fisiche.

Lo script `tools/check-build.ps1` usa i percorsi dell'installazione ESP-IDF locale
e cartelle di build/configurazioni separate per target; serve alla verifica degli strumenti.
Per configurare e caricare normalmente usare i comandi sopra.

Riferimenti: [driver Espressif DS18B20](https://components.espressif.com/components/espressif/ds18b20/versions/0.4.0/readme),
[MQTT discovery Home Assistant](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).
