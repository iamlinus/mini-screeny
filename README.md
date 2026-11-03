# mini screeny

Waveshare **ESP32-S3-LCD-1.69** (240×280 ST7789V2, QMI8658 IMU, buzzer), Arduino via PlatformIO.

## Sidor (PWR byter sida)
1. **Claude-gubben**: en interaktiv figur som styrs av rörelsesensorn
2. **Kjell Group-aktien**: livekurs från Avanza varje minut (grön prick = live, grå = inget på 3 min)
3. **VibePulse**: dina Claude-kvoter (session, vecka, modellvecka) och Codex-veckan, lästa direkt på Macen (grön prick = färsk data, grå = inget på 2 min). Streck betyder att värdet saknas.

## Orientering
Skärmen sitter monterad liggande: panelens högra kant (sett med standardorienteringen 240×280) är golvet. Alla sidor ritas därför på en canvas som roteras med `MOUNT_ROT` i `src/board.h` (3 just nu) och är 280×240 px. Sensorvärdena roteras på samma sätt, så "ner" i koden är golvet i monteringen. Monterar du om kortet räcker det att ändra `MOUNT_ROT` (0–3). Kalibreringen sparas i panelens axlar och behöver inte göras om.

## Companion-servern på Macen
Kortet har inget nätverk. Det sitter i USB-uttaget på skärmen och visar bara det som Macen skickar. `tools/companion.py` körs i bakgrunden, håller USB-porten och skickar en rad per datakälla:

| Rad | Källa | Hur ofta |
|---|---|---|
| `vp {json}` | Claude- och Codex-kvoter, lästa på Macen (`tools/quota.py`) | 30 s (Claude frågas högst var 4:e min) |
| `stk {json}` | Avanzas publika kurs för Kjell Group | 60 s |

Senaste data skickas om varje varv, och allt skickas direkt när kortet startar om (det skriver `mini-screeny boot`). Om en källa inte svarar skickas inget. Kortet behåller då de senaste värdena och pricken blir grå.

**Autostart vid inloggning** (LaunchAgent):
```sh
tools/companion_launchagent.sh install     # installera + starta
tools/companion_launchagent.sh status      # körs den? + senaste loggraderna
tools/companion_launchagent.sh restart     # efter ändringar i companion.py
tools/companion_launchagent.sh uninstall   # ta bort
```
Logg: `~/Library/Logs/mini-screeny-companion.log`. Kör den manuellt med `.venv/bin/python -I tools/companion.py` (`--once` för ett varv, `--vp-fixture fil.json` för att testa VibePulse utan server).

**Kvoterna** (`tools/quota.py`) är lånade från [VibePulse](https://github.com/niclasvestlund-YT/vibepulse) av Niclas Vestlund (MIT, se `tools/LICENSE.vibepulse`). Ingen VibePulse-server behövs:
- **Claude**: Claude Codes inloggning (Claude Desktop eller Nyckelringens "Claude Code-credentials") används mot Anthropics usage-anrop, som bara läser. Det görs högst var 4:e minut, med paus på minst 10 minuter om API:et svarar 429, eftersom kvoten delas med Claude Code. Frågar macOS om Nyckelringen, välj *Tillåt alltid*. Tokenen loggas aldrig.
- **Codex**: `codex app-server` (samma siffror som Codex visar), med `~/.codex/sessions` som reserv. Saknas Codex visas streck.

**Ny datakälla**: skriv en funktion som returnerar en dict, lägg till den i `SOURCES` i `companion.py` och lägg till ett serial-kommando för taggen i firmwaren (som `stk` i `main.cpp`).

**Porten delas**: companion-servern släpper porten automatiskt när `sim.py` behöver den (pausfil i `tools/out/`). Flasha därför med `tools/sim.py flash` och inte direkt med `pio`. Båda öppnar porten exklusivt, så en krock ger ett fel i stället för trasig data.

## Första start: kalibrering
Kortet behöver veta hur sensorn sitter i förhållande till skärmen. Du gör det en gång, och det sparas sedan i flashminnet.
1. Lägg kortet **platt på bordet** med skärmen uppåt och tryck **BOOT** (eller PWR).
2. Håll kortet **upprätt så som det sitter monterat** (golvet nedåt, skärmen mot dig) och tryck **BOOT** (eller PWR).

Gör om kalibreringen genom att **hålla in BOOT i 1,2 s** på gubbsidan.

## Knappar på gubbsidan
| Knapp | Gör |
|---|---|
| PWR | Nästa sida |
| BOOT kort tryck | Ljud av/på (sparas) |
| BOOT långt tryck | Kalibrera om |

## Vad gubben reagerar på
| Gör så här | Händer |
|---|---|
| Luta | Glider åt det håll kortet lutar, "Wiii!", viftar med armarna |
| Vänd upp och ner | Ramlar och tumlar, står i taket: "Allt är upp och ner!". Efter 5 s blir han röd i ansiktet |
| Håll på sidan | Står på väggen |
| Snurra i skärmplanet | Blir yr: spiralögon, stjärnor runt huvudet, lite grön, vinglar |
| Skaka | Studsar mot kanterna, svettas, "Hjäääälp!". Skakar du länge blir han arg (röd, ånga, ögonbryn) |
| Knacka en gång | Hoppar, "Boing!", hjärtan |
| Knacka två gånger snabbt | Dansar med musik och noter |
| Gunga sakta fram och tillbaka | Blir glad, hjärtan, "Mysigt!" |
| Kasta försiktigt (fritt fall) | "AAAAAH!", svävar och tumlar |
| Vänd skärmen nedåt en stund | "Vem släckte?" när du vänder tillbaka |
| Låt den ligga still i ~25 s | Gäspar, somnar (zzz, skärmen dimmas), vaknar när du rör den |
| Låt den vara | Vandrar runt, blinkar, tittar runt, pratar ibland |

## Utveckling
```sh
.venv/bin/python -I tools/sim.py flash     # bygg + flasha (pausar companion-servern under tiden)
.venv/bin/python -I tools/sim.py list       # testscenarier
.venv/bin/python -I tools/sim.py scenario spin shake   # kör med simulerad sensordata -> tools/out/*_sheet.png
.venv/bin/python -I tools/sim.py shot tools/out/now.png  # skärmdump så som skärmen ser ut i monteringen (lägg till `raw` för panelens oroterade bild)
```
Lägg till `-DQUIET_BUILD` i `platformio.ini` för att stänga av allt ljud medan du utvecklar.

Serial-kommandon: `vp {json}`, `stk {json}`, `hello`, `vpdbg`, `sim 1|0`, `sens gx gy gz wz wmag jerk jx jy ff still`, `step N`, `dump`, `reset`, `stat`, `imu`, `dbg`, `calreset`, `calguess`, `page N`.

Moduler: `board.h` (pinnar), `imu.*` (QMI8658-tråd 200 Hz), `frame.*` (kalibrering + sensorvärden i skärmkoordinater),
`buddy.*` (fysik, humör, ritning), `sound.*` (icke-blockerande summer), `stock.*` (aktiesidan), `pulse.*` (VibePulse-sidan), `main.cpp`. På Macen: `tools/companion.py` (datakällor), `tools/boardport.py` (hitta kortet, dela porten), `tools/sim.py` (test).
