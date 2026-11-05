# Mosquitto con Docker Compose

Eseguire questi comandi sul server Docker (shell Linux), nella cartella
`deploy/mosquitto` di questo repository. Il servizio e separato dal Compose
di Home Assistant: non occorre modificarlo. La porta TCP 1883 deve essere libera.

## Primo avvio

```sh
docker compose pull
docker compose run --rm --no-deps --user 0 --entrypoint mosquitto_passwd mosquitto -c /mosquitto/config/passwords homeassistant
docker compose run --rm --no-deps --user 0 --entrypoint mosquitto_passwd mosquitto /mosquitto/config/passwords esp32
docker compose run --rm --no-deps --user 0 --entrypoint sh mosquitto -c 'chown 1883:1883 /mosquitto/config/passwords && chmod 600 /mosquitto/config/passwords'
docker compose up -d
docker compose logs --tail=50 mosquitto
```

I due comandi `mosquitto_passwd` chiedono le rispettive password, da digitare
e confermare senza inserirle nella riga di comando. Il primo comando con `-c`
crea il file: non ripeterlo su un broker gia configurato perche lo sovrascrive.
I comandi `run` preparano gli utenti senza avviare il broker.
Il file passwords resta sul server ed e escluso da Git.

Se Docker richiede sudo, aggiungerlo davanti a ciascun comando docker.
Il volume `mqtt_data` conserva messaggi retained e dati del broker ai riavvii;
non usare `docker compose down -v` se vuoi conservarli.

## Home Assistant

1. Aprire **Impostazioni > Dispositivi e servizi > Aggiungi integrazione > MQTT**.
2. Inserire l'IP LAN del server Docker come broker, porta `1883`,
   utente `homeassistant` e la password scelta.
3. Lasciare discovery attiva con prefisso `homeassistant`.

Usare l'IP del server, non l'IP interno del container. Non usare `localhost`
se Home Assistant gira su una rete bridge Docker: indicherebbe il container stesso.
L'indirizzo IP del server deve essere raggiungibile dalla rete del container HA.

## ESP32

In `idf.py menuconfig`, menu **Centrale termica**, impostare:

- Broker: `mqtt://IP_DEL_SERVER:1883` (sostituire IP_DEL_SERVER).
- Utente: `esp32`.
- Password: quella scelta per esp32.
- SSID e password della rete Wi-Fi.

Con sonde collegate e funzionanti il firmware pubblica automaticamente
dispositivo e sensori nell'integrazione MQTT, entro un ciclo di lettura.
Senza sonde non vengono create entita di temperatura.

## Cambiare una password

```sh
docker compose run --rm --no-deps --user 0 --entrypoint mosquitto_passwd mosquitto /mosquitto/config/passwords esp32
docker compose run --rm --no-deps --user 0 --entrypoint sh mosquitto -c 'chown 1883:1883 /mosquitto/config/passwords && chmod 600 /mosquitto/config/passwords'
docker compose restart mosquitto
```

Questa configurazione usa MQTT senza TLS sulla LAN e non limita i topic per
utente. Mantenere la porta 1883 sulla rete locale senza inoltrarla da Internet.

Riferimenti: [immagine ufficiale](https://hub.docker.com/_/eclipse-mosquitto),
[password Mosquitto](https://mosquitto.org/documentation/authentication-methods/),
[integrazione MQTT](https://www.home-assistant.io/integrations/mqtt/).
