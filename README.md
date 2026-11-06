# Temperature centrale termica

Per installare il broker sul server Docker seguire la [guida Docker Compose](deploy/mosquitto/README.md).

Firmware ESP-IDF 6.1 per ESP32-WROOM (`esp32`), ESP32-C3 ed ESP32-C6.
Fino a 8 DS18B20 su un unico bus 1-Wire; letture ogni 15 secondi,
invio MQTT e discovery Home Assistant.

Dalla versione **0.3.0** il target `esp32` include il profilo per la
[scheda ESP32 Relay X4 gia installata](docs/scheda-4-rele.md): un rele replica
localmente il contatto pulito del flussostato, mentre gli altri restano OFF.
Non sono previste forzature di avvio o comandi remoti del rele.
Stato flussostato e diagnostica di boot vengono inviati a Home Assistant;
il conteggio dei sei mesi per la pulizia resta interamente in HASS.

## Preparazione

Aprire questa cartella in VS Code con l'estensione Espressif.
Nel terminale ESP-IDF, dopo aver scelto la scheda:

```powershell
idf.py set-target esp32c6
idf.py menuconfig
```

Per WROOM usare `esp32`, per C3 usare `esp32c3`.
Su `esp32` il profilo Relay X4 e abilitato per default: per una scheda WROOM
generica disabilitarlo in **Centrale termica > Scheda ESP32 Relay X4**.
Il [pinout e comportamento Relay X4](docs/scheda-4-rele.md) sostituisce le
indicazioni C6 per LED, ingresso flussostato e numerazione rele.
Per C6 il file `sdkconfig.defaults.esp32c6` seleziona flash da 4 MiB,
la tabella `partitions.csv` con due slot OTA da 1920 KiB e il rollback di avvio.
Sono adatti sia ESP32-C6-DevKitC-1 sia ESP32-C6-DevKitM-1, con almeno 4 MiB di flash.
Se si riusa un vecchio sdkconfig C6, selezionare in menuconfig
**Partition Table > Custom partition table CSV**, file `partitions.csv`,
**Serial flasher config > Flash size > 4 MB** e
**Bootloader config > Enable app rollback support**.
In **Centrale termica** selezionare GPIO pulsante `10` e LED RGB `8`:
i valori gia presenti in sdkconfig non vengono sostituiti dai nuovi default.
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

1. Con scheda gia avviata tenere premuto **il pulsante AP/reset per almeno 5 secondi
   e meno di 15**, poi rilasciarlo. Su C6 il default e un pulsante esterno tra GPIO10 e GND.
   Su WROOM/C3 il default resta BOOT: non tenerlo premuto durante accensione/reset.
2. Il nome rete e `Centrale-Setup-XXXXXX`, con suffisso derivato dal MAC della scheda.
   Dalla versione 0.2.1 la password predefinita e **`CentraleSetup2026`**:
   resta uguale dopo riavvio, reset della configurazione ed erase-flash.
   Si modifica prima della compilazione in **Centrale termica > Password fissa rete AP di setup**.
   Il QR Wi-Fi della scheda permette di collegarsi senza digitarla; poi aprire la pagina al punto 4.
3. Dal telefono collegarsi a quella rete WPA2, rimanendo collegati anche se segnala assenza di Internet.
4. Aprire esplicitamente **http://192.168.4.1** nel browser. Non e previsto captive portal automatico.
5. Impostare SSID/password e broker MQTT, ad esempio `mqtt://192.168.2.22:1883`, utente `esp32` e password.
6. Premere **Salva e riavvia**. Il setup si chiude al riavvio e la scheda si ricollega.

Le password attuali non vengono restituite alla pagina: lasciare il campo vuoto
le mantiene; quando si cambia rete bisogna inserire la nuova password.
Le caselle apposite permettono di usare una rete aperta o cancellare la password MQTT.
Le impostazioni vengono salvate insieme in NVS, mantenute agli aggiornamenti normali
del firmware e hanno precedenza sui valori di menuconfig. Un erase-flash completo
cancella queste impostazioni, ma la password AP resta quella compilata nel firmware.

Il setup si apre anche automaticamente senza SSID oppure dopo 45 secondi senza
connessione Wi-Fi (all'avvio o dopo una disconnessione), permettendo di correggere
credenziali errate. Resta aperto fino al riavvio; le letture continuano e l'eventuale
connessione alla rete domestica viene ristabilita dopo l'attivazione AP.
Una password MQTT errata, da sola, non apre automaticamente il setup:
usare il pulsante AP/reset per modificarla. La pagina accetta richieste solo sull'indirizzo AP,
non sull'IP della scheda nella rete domestica.

Il pulsante e attivo basso con pull-up interno: GPIO10 esterno su C6,
GPIO0 (BOOT) sul WROOM e GPIO9 (BOOT) su C3.
Collegare un pulsante normalmente aperto tra GPIO10 e GND; non serve una resistenza esterna.
Verificare il pinout della scheda. Modificarlo in **Centrale termica > GPIO pulsante AP/reset**
oppure impostare `-1` per disabilitarlo; non usare i GPIO delle sonde o del LED RGB.
GPIO sonde e intervallo letture restano configurabili da menuconfig.

Tenendo premuto per **almeno 15 secondi**, il LED lampeggia rapidamente in rosso;
al rilascio vengono cancellate rete Wi-Fi e credenziali MQTT.
La scheda si riavvia direttamente in setup, anche se nel firmware erano compilate credenziali.
La password AP predefinita resta uguale e il QR Wi-Fi stampato continua a funzionare.
Il reset conserva firmware, calibrazione radio e identita MAC; non elimina le entita Home Assistant.
Pressioni brevi e pulsante gia premuto all'avvio non eseguono azioni.

L'[etichetta della scheda FB75D8](docs/etichette/centrale-FB75D8-etichetta.png)
contiene un QR per collegarsi al Wi-Fi e uno per aprire il setup dopo la connessione.
E disponibile anche il [QR Wi-Fi vettoriale](docs/etichette/centrale-FB75D8-wifi.svg).
Per rigenerarla o prepararla per un'altra scheda, usando il MAC base Wi-Fi a sei byte:

```powershell
python -m pip install --target build-qr-deps qrcode pillow
python tools/generate-setup-qr.py --mac cc:8d:a2:fb:75:d8
```

Lo script legge la password da `sdkconfig.esp32c6`; generare l'etichetta dopo la configurazione
del firmware. Se e disponibile `zxing-cpp`, verifica anche la decodifica dei due QR.
Dopo il passaggio dalla vecchia password casuale a quella predefinita, dimenticare
la rete salvata sul telefono e collegarsi nuovamente usando il QR.

## Connessione e LED RGB

La riconnessione Wi-Fi avviene senza bloccare le letture: primo tentativo immediato,
poi attese di 1, 2, 4, 8, 16, 32 e 60 secondi tra i fallimenti, con massimo 60 secondi.
Dopo circa 10 minuti riparte un ciclo rapido. Ai tempi di attesa si aggiunge il tempo
del singolo tentativo; un tentativo senza IP viene interrotto dopo 30 secondi.
Il backoff viene azzerato dopo 60 secondi di connessione stabile, evitando che brevi
connessioni intermittenti provochino tentativi continui. Il passaggio ad AP+STA non
azzera il backoff. MQTT mantiene la riconnessione automatica della libreria (10 secondi).
Le letture offline vengono registrate sulla seriale, senza uno storico da reinviare.

Su C6 il LED indirizzabile WS2812 integrato e pilotato da GPIO8.
GPIO e luminosita sono configurabili; `-1` disabilita il LED (default su WROOM/C3).

| Segnale | Stato |
| --- | --- |
| Rosso lampeggiante | Wi-Fi senza IP / riconnessione |
| Giallo fisso | Wi-Fi con IP, MQTT scollegato |
| Verde fisso | Wi-Fi e MQTT collegati |
| Blu alternato al colore di connessione | AP di setup attivo |
| Blu lampeggiante durante la pressione | Rilascio pronto per aprire AP (5-15 s) |
| Rosso rapido durante la pressione | Rilascio pronto per reset (almeno 15 s) |
| Viola fisso | Caricamento e verifica OTA |

## Aggiornamenti OTA dal browser (C6)

Il **primo caricamento va eseguito via USB** con questa tabella delle partizioni,
bootloader e applicazione (`idf.py -p COM5 flash`). La partizione NVS conserva posizione
e dimensione della tabella standard precedente; non e richiesto un erase-flash.
Il firmware precedente con partizione singola non puo acquisire la nuova tabella via OTA.
Su ESP32 Relay X4 il file `sdkconfig.defaults.esp32` seleziona gia la stessa tabella
OTA e il rollback; verificare prima la flash reale, di almeno 4 MiB.
Su C3 queste opzioni restano da selezionare manualmente in menuconfig.

Per gli aggiornamenti successivi:

1. Compilare per `esp32c6` con `idf.py build` e la stessa tabella OTA.
2. Aprire AP con il pulsante, collegarsi alla rete protetta e visitare `http://192.168.4.1`.
3. In **Aggiorna firmware** selezionare `build/mux_temperatura.bin` e premere **Carica firmware e riavvia**.
   Con lo script di verifica il file e `build-esp32c6/mux_temperatura.bin`.
4. Attendere verifica e riavvio. SSID, MQTT e password AP vengono conservati.

Caricare soltanto il binario applicativo, mai bootloader, tabella partizioni o immagini fuse.
Il firmware viene scritto nello slot inattivo e controllato da ESP-IDF; vengono rifiutati
immagini per un altro chip, progetto differente, file incompleti o troppo grandi.
L'endpoint e accessibile solo dalla rete AP, con token di sessione; durante il caricamento
il pulsante viene ignorato. Il browser funziona anche senza collegamento a Internet.
Se il trasferimento fallisce lo slot attuale resta selezionato. Con rollback abilitato,
un nuovo firmware che si riavvia prima della conferma di avvio lascia tornare quello precedente.
La conferma avviene al termine del primo ciclo di lettura: non richiede Wi-Fi, broker o sonde presenti.
Non e una validazione completa del funzionamento sul campo o una firma crittografica del firmware.
Le future immagini devono conservare l'OTA e la conferma di avvio per poter aggiornare ancora.

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

Per le funzioni introdotte nella versione 0.2.0 e verificata la compilazione C6
con LED RGB, doppia partizione OTA e rollback, incluso il controllo dimensioni.
La suite attuale comprende nove test host sul codice C di backoff, pulsante e replica flussostato:
limite e ripartenza del backoff, uptime oltre 32 bit, soglie al rilascio,
pulsante premuto all'avvio e rimbalzo del contatto, piu avvio del flussostato aperto/chiuso,
conferma di chiusura, apertura senza ritardo e rimbalzi. Con MSVC installato:

```powershell
python tests/test_control_policy.py
```

Le prove fisiche delle nuove funzioni restano da eseguire: colori del LED,
pulsante esterno GPIO10 (AP e reset), router spento oltre 10 minuti e successivo
recupero, OTA valido, immagine per chip/progetto errato, trasferimento interrotto
e rollback. Verificare inoltre il funzionamento contemporaneo di LED RMT e sonde.
Le verifiche WROOM riportate sotto si riferiscono alla versione precedente.

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
[ESP32-C6-DevKitC-1 e LED GPIO8](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html),
[MQTT discovery Home Assistant](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).
