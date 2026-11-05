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
Per C6 il file `sdkconfig.defaults.esp32c6` seleziona la partizione applicativa
standard da 1500 KiB, necessaria per includere il setup web (flash minima 2 MiB).
Se si riusa un vecchio sdkconfig C6, selezionare in menuconfig
**Partition Table > Single factory app (large)** prima di compilare.
Nel menu **Centrale termica** impostare SSID e password Wi-Fi, URI del broker
MQTT (esempio `mqtt://192.168.1.10:1883`), utente/password e GPIO dati.
SSID vuoto apre la rete di configurazione; le letture seriali restano disponibili.
MQTT viene avviato dopo l'assegnazione dell'IP Wi-Fi.
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

## Configurazione dal telefono

Non occorre ricompilare per cambiare rete o credenziali MQTT:

1. Con scheda gia avviata tenere premuto **BOOT per 5 secondi**, poi rilasciarlo.
   Non tenerlo premuto durante accensione/reset: su queste schede serve anche al bootloader.
2. Nel monitor seriale leggere nome rete `Centrale-Setup-XXXXXX` e password setup.
   La password e casuale, propria della scheda, viene conservata in NVS e resta uguale ai riavvii.
3. Dal telefono collegarsi a quella rete WPA2, rimanendo collegati anche se segnala assenza di Internet.
4. Aprire esplicitamente **http://192.168.4.1** nel browser. Non e previsto captive portal automatico.
5. Impostare SSID/password e broker MQTT, ad esempio `mqtt://192.168.2.22:1883`, utente `esp32` e password.
6. Premere **Salva e riavvia**. Il setup si chiude al riavvio e la scheda si ricollega.

Le password attuali non vengono restituite alla pagina: lasciare il campo vuoto
le mantiene; quando si cambia rete bisogna inserire la nuova password.
Le caselle apposite permettono di usare una rete aperta o cancellare la password MQTT.
Le impostazioni vengono salvate insieme in NVS, mantenute agli aggiornamenti normali
del firmware e hanno precedenza sui valori di menuconfig. Un erase-flash completo
cancella anche queste impostazioni e la password del setup.

Il setup si apre anche automaticamente senza SSID oppure dopo 45 secondi senza
connessione Wi-Fi (all'avvio o dopo una disconnessione), permettendo di correggere
credenziali errate. Resta aperto fino al riavvio; le letture continuano e l'eventuale
connessione alla rete domestica viene ristabilita dopo l'attivazione AP.
Una password MQTT errata, da sola, non apre automaticamente il setup:
usare BOOT per modificarla. La pagina accetta richieste solo sull'indirizzo AP,
non sull'IP della scheda nella rete domestica.

Il pulsante e attivo basso con pull-up interno: GPIO 0 sul WROOM e GPIO 9 su C3/C6.
Verificare il pinout della scheda. Modificarlo in **Centrale termica > GPIO pulsante setup**
oppure impostare `-1` per disabilitarlo; non usare lo stesso GPIO delle sonde.
GPIO sonde e intervallo letture restano configurabili da menuconfig.

## Collegamenti

Usare sonde a tre fili: VDD a 3,3 V, GND comune e DQ al GPIO scelto.
Inserire una resistenza esterna da 4,7 kohm tra DQ e 3,3 V.
Tutte le sonde condividono alimentazione e linea dati. La modalità parassita
non è supportata. Verificare il pinout reale delle sonde, senza affidarsi ai colori.
Il firmware abilita anche il pull-up interno per mantenere stabile il GPIO
quando si prova la scheda senza cablaggio: non sostituisce quello esterno.
Senza sonde registra lo stato di attesa e non avvia conversioni.
Dopo un timeout o uno stato RMT non valido reinizializza il bus al termine
del ciclo, conservando gli indirizzi delle sonde gia note.
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
Verificato su WROOM via seriale: avvio senza sonde per tre cicli,
connessione Wi-Fi e MQTT al broker reale, senza timeout RMT o errori di stato.
Verificati sul WROOM anche apertura dell'AP con pressione BOOT simulata via USB,
avvio del server setup e rifiuto HTTP 403 delle richieste dall'IP LAN domestico.
La pagina supporta i socket IPv4 e quelli IPv4 mappati su IPv6 usati da ESP-IDF.
Confermati dal telefono apertura della pagina e salvataggio della configurazione.
Verificati poi via seriale, dopo un ulteriore riavvio, caricamento dei dati NVS,
riconnessione Wi-Fi e MQTT e assenza dell'AP quando la rete domestica funziona.
Restano da verificare letture con sonde reali e recupero da guasti del cablaggio.

Prima dell'installazione verificare lettura delle 8 sonde, nomi dopo riavvio,
scollegamento e riconnessione di una sonda, perdita Wi-Fi, riavvio broker e Home Assistant.
La compilazione non sostituisce queste verifiche fisiche.

Lo script `tools/check-build.ps1` usa i percorsi dell'installazione ESP-IDF locale
e cartelle di build/configurazioni separate per target; serve alla verifica degli strumenti.
Per configurare e caricare normalmente usare i comandi sopra.

Riferimenti: [driver Espressif DS18B20](https://components.espressif.com/components/espressif/ds18b20/versions/0.4.0/readme),
[MQTT discovery Home Assistant](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).
