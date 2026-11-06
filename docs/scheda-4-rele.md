# ESP32 Relay X4: sonde e replica flussostato

Profilo per la scheda gia installata, basato sul
[pinout ESPHome Devices](https://devices.esphome.io/devices/esp32-relay-x4/).
Il target e **esp32**, non esp32c6. Il vecchio progetto `gpio_4relay_esp32`
e stato consultato come riferimento e non e stato modificato.

## Collegamenti

Disponibili lo [schema dei collegamenti e la mappa dei pin in PDF](schema/schema-relay-x4.pdf)
e le [note sulle viste fronte/retro e sui due LED extra](schema/README.md).
GPIO23 e documentato per il LED di stato; il collegamento dell'altro LED extra
resta da verificare elettricamente. Anche GPIO22 va controllato sulla propria
revisione prima di utilizzarlo come ingresso del flussostato.

| Funzione | GPIO / morsetti |
| --- | --- |
| DQ di tutte le DS18B20 | GPIO4, pull-up esterna 4,7 kohm verso 3V3 |
| Contatto pulito flussostato | GPIO22 e GND, pull-up interna |
| LED integrato semplice | GPIO23, attivo alto |
| Pulsante AP/reset | GPIO0 e GND (BOOT, oppure pulsante esterno) |
| Rele fisico 1 | GPIO32, mantenuto OFF |
| Rele fisico 2 | GPIO33, mantenuto OFF |
| Rele fisico 3 | GPIO25, mantenuto OFF |
| Rele fisico 4, replica flussostato | GPIO26, attivo alto |
| Collegamento verso ingresso PDC | COM e NO del rele fisico 4 |

Il vecchio array era `{26,25,33,32}`: il suo indice zero corrispondeva al
**rele fisico 4**, non al rele fisico 1. Il vecchio click-clack usava GPIO32,
cioe il rele fisico 1. Quella sequenza viene eliminata.
Se il collegamento reale verso la PDC e su un altro rele, impostare il numero
fisico corretto in **Centrale termica > Numero rele che replica il flussostato**.
Confermare COM/NO prima di mettere in servizio il nuovo firmware.

Il flussostato entra come contatto pulito esclusivamente sul lato ESP32;
l'ingresso della PDC riceve il contatto isolato COM/NO del rele, senza unire
le masse delle due schede attraverso quel collegamento.

## Comportamento locale

All'inizio dell'applicazione tutti i GPIO dei rele vengono inizializzati OFF,
prima della configurazione di rete. Il rele 4 segue poi il flussostato:

- contatto aperto: rele OFF, COM/NO aperti;
- contatto chiuso stabilmente per 50 ms: rele ON, COM/NO chiusi;
- riapertura: rele OFF al primo campionamento, senza ritardo aggiuntivo;
- ingresso disabilitato (`GPIO=-1`): rele sempre OFF.

Il task di controllo campiona nominalmente ogni 10 ms. Non attende Wi-Fi,
MQTT, conversioni delle sonde, setup o messaggi da Home Assistant. Le pubblicazioni
passano attraverso una coda separata e non bloccano quel task. La conferma di
chiusura e configurabile da 0 a 1000 ms. Non esiste piu l'attesa di 13,4 s del
vecchio firmware, ne una chiusura forzata in assenza del contatto.
I tempi fisici di commutazione del rele si aggiungono a quelli software.
Il comportamento dei contatti durante accensione, reset e perdita alimentazione
va verificato sulla scheda e sul cablaggio reale.

Non sono esposti comandi MQTT per forzare il rele. Il reset AP cancella rete/MQTT,
non il contatore storico degli avvii. La password fissa AP e l'OTA restano disponibili.

## LED di connessione, senza RGB

| Segnale | Stato |
| --- | --- |
| Acceso fisso | Wi-Fi e MQTT collegati |
| Un breve lampeggio ogni 2 s | Wi-Fi senza IP |
| Lampeggio lento, 0,5 s acceso / 0,5 s spento | Wi-Fi collegato, MQTT scollegato |
| Due brevi lampeggi ogni 2 s | AP di setup aperto, precedenza sullo stato di rete |
| Lampeggio rapido durante pressione, da 5 s | Pronto ad aprire AP al rilascio |
| Lampeggio ancora piu rapido, da 15 s | Pronto a resettare configurazione al rilascio |
| Lampeggio molto rapido durante OTA | Scrittura/verifica firmware |

Sul profilo C6 il LED RGB precedente rimane supportato; su Relay X4 e disabilitato.

## Home Assistant e manutenzione

Le temperature restano nello stesso dispositivo MQTT `centrale_<MAC>`.
La discovery aggiunge:

- **Flussostato chiuso**: stato letto dal contatto;
- **Flussostato chiuso al boot**: primo campionamento dopo la configurazione dell'ingresso;
- **Comando rele flussostato**: comando ON/OFF inviato alla bobina, non feedback elettrico dei contatti;
- **Numero avvii**: contatore NVS, incrementato una volta per avvio, anche se offline;
- **Causa riavvio**: ad esempio `power_on`, `software`, `brownout`, `panic`, `task_watchdog`;
- **Tempo dall'avvio**: secondi di uptime.

Stato e discovery vengono ripubblicati ogni 15 s e al ritorno della connessione;
le variazioni di ingresso/comando vengono inviate senza attendere il ciclo sonde.
Topic JSON retained: `centrale/<nodo>/plant/state`.
Disponibilita: topic del nodo con Last Will, come per le sonde. Quando l'ingresso
e disabilitato, le entita relative al flussostato risultano non disponibili.
I messaggi di stato riportano l'ultimo stato, non uno storico di tutte le transizioni offline.

L'avvio e quello della scheda ESP32, non una misura dell'accensione della PDC.
Un `power_on` puo indicare il ripristino dell'alimentazione, ma non distingue da solo
un blackout da uno spegnimento manuale. Un contatto aperto puo essere normale se
la pompa e ferma; Home Assistant puo correlarlo con lo stato della PDC.

Il promemoria di pulizia filtri/impianto **ogni sei mesi viene calcolato da Home Assistant**.
Il firmware non avvia timer di manutenzione e non modifica lo stato del rele in base
alla scadenza. In HASS mantenere una data "Ultima pulizia" e una conferma "Pulizia eseguita";
i riavvii dell'ESP32 non devono spostare la scadenza.

## Compilazione e prove

Versione 0.3.0 compilata con ESP-IDF 6.1 per `esp32`, con ingresso GPIO22,
uscita rele fisico4/GPIO26 e LED23. Binario applicativo da 932448 byte,
con 53% libero nello slot OTA. Superati nove test host, inclusi quattro casi
di replica flussostato. Non e stato effettuato un caricamento sulla Relay X4;
le verifiche sul cablaggio reale restano da eseguire.

Verifica aggiuntiva su scheda di sviluppo ESP32-WROOM su COM42, senza rele o
sonde collegati: flash identificata da 4 MiB; caricamento 0.3.0 e avvio riusciti;
GPIO22 aperto letto come aperto; un ponticello tra GPIO22 e GND ha prodotto nei
log il comando ON dopo 50 ms, seguito da OFF all'apertura. Durante la manipolazione
del ponticello si sono viste commutazioni dovute alle interruzioni del contatto.
Dopo riavvio, GPIO22 aperto e comando OFF. Questa prova non verifica un rele
fisico, i morsetti verso la PDC o il flussostato reale. La scheda non si e collegata
alla rete configurata e ha aperto l'AP `Centrale-Setup-52C8C4`; l'etichetta QR e
specifica di questa scheda di sviluppo, non della Relay X4 installata.
Con una DS18B20 collegata a GPIO4 e il solo pull-up interno, sono state lette
tre temperature consecutive: 25,69; 25,75; 25,81 °C. La prova e preliminare:
per il collegamento definitivo resta necessaria la resistenza esterna da 4,7 kohm.

Per una nuova configurazione:

```powershell
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

Il profilo di default `sdkconfig.defaults.esp32` seleziona Relay X4, LED23,
flussostato22, rele fisico4 e la tabella OTA con flash minima **4 MiB**.
Le configurazioni gia esistenti vanno aggiornate in menuconfig: i default non
sostituiscono i valori salvati. Il vecchio progetto dichiarava 2 MiB di flash;
questa impostazione non dimostra la capacita effettiva del modulo.
Prima del flash leggere il chip e la capacita con `esptool flash-id` e verificare
che ci siano almeno 4 MiB per questa tabella OTA.

La porta COM41 usata in precedenza appartiene alla C6: non viene riutilizzata
automaticamente per questa scheda ESP32. Il primo caricamento Relay X4 richiede
USB-UART a livelli logici 3,3 V e la procedura di boot della scheda.
L'etichetta QR della C6 ha un altro MAC e non e quella della Relay X4:
rigenerarla con il MAC base della nuova scheda e `--sdkconfig sdkconfig.esp32`.

Prima del collegamento operativo verificare con un contatto di prova e un tester:
avvio a contatto aperto/chiuso, rimbalzi, apertura del filo, relays 1-3 sempre OFF,
assenza di commutazioni cicliche, perdita Wi-Fi/broker, letture di tutte le sonde,
aggiornamento OTA e corrispondenza dei morsetti COM/NO. La compilazione e i test
della logica non sostituiscono queste prove fisiche.
