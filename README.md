# mini screeny

En liten skärm som sitter i USB-uttaget på datorskärmen: en Claude-gubbe som reagerar på rörelser, en aktiekurs, dina Claude-kvoter och ditt fotbollslag. Skärmen har inget eget nätverk. Ett litet program på Macen (companion-servern) hämtar datan och skickar den över USB-kabeln.

Hårdvara: Waveshare **ESP32-S3-LCD-1.69** (240×280 ST7789V2, QMI8658 IMU), Arduino via PlatformIO.

## Installera på datorn (en gång)
Koppla in skärmen med USB. Visar den "Installera på datorn" saknas companion-servern. Enklast är att be **Claude Code**:

> Installera mini screeny från github.com/iamlinus/mini-screeny

(Claude följer [`INSTALL.md`](INSTALL.md).) Eller för hand i Terminal:
```sh
git clone https://github.com/iamlinus/mini-screeny.git ~/mini-screeny
~/mini-screeny/install.sh
```
`install.sh` skapar en Python-miljö (hämtar Python 3.12 via `uv`), startar companion-servern nu och vid varje inloggning, och öppnar inställningssidan. Frågar macOS om Nyckelringen ("Claude Code-credentials"), välj *Tillåt alltid*: det är så Claude-kvoterna läses. Uppdatera: `git pull` och kör `install.sh` igen. Ta bort: `tools/companion_launchagent.sh uninstall`.

## Sidor (PWR byter sida)
1. **Gubben**: en interaktiv figur som styrs av rörelsesensorn och hälsar med ditt namn
2. **Aktien**: livekurs från Avanza varje minut
3. **Claude**: dina Claude-kvoter (5-timmarssessionen, veckan och, när Anthropic har en egen veckogräns för en modell, den modellens vecka), lästa direkt på Macen. Streck betyder att värdet saknas.
4. **Fotboll**: lagets tabellplacering, dagar kvar till nästa match (H = hemma, B = borta) och senaste resultatet (V/O/F). Data från ligornas officiella källa (samma som allsvenskan.se och superettan.se), hämtas vid start och sedan varje timme.

## Inställningar
Öppna **http://localhost:8740** i webbläsaren (när companion-servern körs). Där väljer du namn, fotbollslag (rullista), aktie (sök hos Avanza) och vilka sidor som visas och i vilken ordning, och ser om skärmen är ansluten och hur färsk datan är. Sidan går bara att nå från den egna Macen.

Inställningarna sparas i `config.toml` i projektmappen, som också går att redigera för hand. Spara filen så slår ändringen igenom inom en minut, utan omstart. Kortet minns namn och sidor även när det startas utan Macen.
- Lag: namn eller förkortning från Allsvenskan/Superettan. Lista: `.venv/bin/python -I tools/companion.py --lag`
- Aktie: sökord hos Avanza (eller `id`). Sök: `.venv/bin/python -I tools/companion.py --aktie "volvo"`

Uppe till höger på datasidorna: **grön prick** = färsk data, **röd prick** = datan är för gammal (5 minuter, för fotbollen 3 timmar) (Macen svarar men får inte fram ny data), **ø** = ingen kontakt med Macen (companion-servern skickar ett livstecken var 10:e sekund).

## Companion-servern på Macen
Kortet har inget nätverk. Det sitter i USB-uttaget på skärmen och visar bara det som Macen skickar. `tools/companion.py` körs i bakgrunden, håller USB-porten och skickar en rad per datakälla:

| Rad | Källa | Hur ofta |
|---|---|---|
| `vp {json}` | Claude-kvoter, lästa på Macen (`tools/quota.py`) | 30 s (Claude frågas högst var 4:e min) |
| `cfg {json}` | namn och sidordning från `config.toml` | 60 s och när filen ändras |
| `stk {json}` | aktien i `config.toml`, från Avanza (`tools/stock.py`) | 60 s |
| `fb {json}` | laget i `config.toml`, från `gql.sportomedia.se` (`tools/football.py`) | vid start, sedan 1 h |

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

**Ny datakälla**: skriv en funktion som returnerar en dict, lägg till den i `SOURCES` i `companion.py` och lägg till ett serial-kommando för taggen i firmwaren (som `stk` i `main.cpp`).

**Porten delas**: companion-servern släpper porten automatiskt när `sim.py` behöver den (pausfil i `tools/out/`). Flasha därför med `tools/sim.py flash` och inte direkt med `pio`. Båda öppnar porten exklusivt, så en krock ger ett fel i stället för trasig data.

## Första start: kalibrering
Kortet behöver veta hur sensorn sitter i förhållande till skärmen. Du gör det en gång, och det sparas sedan i flashminnet.
1. Lägg kortet **platt på bordet** med skärmen uppåt och tryck **BOOT** (eller PWR).
2. Håll kortet **upprätt så som det sitter monterat** (golvet nedåt, skärmen mot dig) och tryck **BOOT** (eller PWR).

Gör om kalibreringen genom att **hålla in BOOT i 1,2 s** på gubbsidan.

## Knappar
| Knapp | Gör |
|---|---|
| PWR | Nästa sida |
| BOOT ett tryck | Hämtar all data på nytt (Claude-kvoterna högst en gång i minuten) |
| BOOT två snabba tryck | Öppnar inställningssidan i webbläsaren på Macen |
| BOOT tre snabba tryck | Öppnar projektet på GitHub |
| BOOT långt tryck (på gubbsidan) | Kalibrera om |

Skärmen är tyst (öppet kontorslandskap). Ljudet finns kvar i koden: lägg till `-DWITH_SOUND` i `build_flags` i `platformio.ini` för att slå på det.

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

Serial-kommandon (dator → kort): `cfg {json}`, `vp {json}`, `stk {json}`, `fb {json}`, `hello`, `ping`, `vpdbg`, `sim 1|0`, `sens gx gy gz wz wmag jerk jx jy ff still`, `step N`, `dump`, `reset`, `stat`, `imu`, `dbg`, `calreset`, `calguess`, `page N`, `bootclicks N` (test). Kort → dator: `evt refresh` (BOOT ×1), `evt settings` (BOOT ×2), `evt github` (BOOT ×3), `mini-screeny boot`.

Moduler: `board.h` (pinnar), `imu.*` (QMI8658-tråd 200 Hz), `frame.*` (kalibrering + sensorvärden i skärmkoordinater),
`buddy.*` (fysik, humör, ritning), `sound.*` (icke-blockerande summer), `stock.*` (aktiesidan), `pulse.*` (VibePulse-sidan), `football.*` (fotbollssidan), `link.*` (statusmarkeringen), `theme.h` (färgerna för alla sidor), `main.cpp`. På Macen: `tools/companion.py` (datakällor), `tools/webconfig.*` (inställningssidan), `tools/config.py` (`config.toml`), `tools/boardport.py` (hitta kortet, dela porten), `tools/sim.py` (test).
