# Schema dei collegamenti ESP32 Relay X4

- [PDF A4 orizzontale, due pagine](schema-relay-x4.pdf)
- [Collegamenti, PNG](01-collegamenti-relay-x4.png) / [SVG vettoriale](01-collegamenti-relay-x4.svg)
- [Pinout retro, PNG](02-pinout-retro-relay-x4.png) / [SVG vettoriale](02-pinout-retro-relay-x4.svg)

La prima pagina rappresenta le connessioni esterne previste dal firmware 0.3.0,
non lo schema elettrico completo dei componenti interni. La seconda trascrive
i pin dei due connettori visti dal **retro**, come nella foto di riferimento.
I morsetti dei rele sono invece riportati separatamente in **vista frontale**,
seguendo le sigle NO/COM/NC leggibili nella foto Amazon. Non confondere le due viste.
Le lettere A/B e i riferimenti riga-L/R sono introdotti dal disegno e non sono
designatori stampati sulla scheda.

## LED extra: cosa e verificato

Il pinout ESPHome documenta un LED di stato su **GPIO23**. Le foto Amazon mostrano
due LED extra con serigrafie che si leggono come **D13 e D14**, oltre ai quattro
indicatori dei rele. Non e stato possibile verificare dalla sola foto quale dei
due sia collegato a GPIO23, ne il circuito dell'altro: l'ipotesi che sia un LED di
alimentazione resta da confermare. L'assenza di GPIO22 dalla tabella dei LED
non dimostra che sia elettricamente libero su ogni revisione.

Per identificare i collegamenti, a scheda completamente disalimentata e scollegata
dalla rete, misurare la continuita dal pin GPIO23 ai due capi delle resistenze in
serie ai LED; il capo direttamente connesso al GPIO identifica il ramo comandato.
Verificare anche GPIO22 e le linee 3V3/5V per l'altro ramo. Non dedurre il circuito
dal solo fatto che un LED sia acceso fisso. Se GPIO22 risultasse collegato a un LED,
spostare l'ingresso flussostato su un GPIO libero e aggiornare il firmware prima dell'uso.

GPIO22 nello schema e quindi un'assegnazione del progetto **da verificare sul proprio
esemplare**, non una verifica elettrica gia eseguita. Nessuna prova e stata effettuata
sulla scheda fisica e non e stato modificato il firmware operativo.

## Fonti e rigenerazione

- [Pinout e LED GPIO23, ESPHome Devices](https://devices.esphome.io/devices/esp32-relay-x4/)
- [Immagine posteriore di riferimento](https://devices.esphome.io/_astro/ac-dc-esp32-relayx4-back.D2uXN5Ux_1cNdHa.webp)
- [Foto della variante in vendita, Amazon B0F59T84HK](https://www.amazon.it/Fasizi-Bluetooth-Programmabile-ESP32-WROOM-32E-Intelligente/dp/B0F59T84HK)

Rigenerare con `python tools/draw-wiring.py` (Pillow). PNG a 308 dpi e PDF A4
orizzontale; SVG per ingrandimento senza perdita di definizione.
