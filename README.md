# mini screeny

Waveshare **ESP32-S3-LCD-1.69** (240×280 ST7789V2, QMI8658 IMU, buzzer), Arduino via PlatformIO.

## Sidor (PWR byter sida)
1. **Claude-gubben**: en interaktiv figur som styrs av rörelsesensorn
2. **Kjell Group-aktien**: hämtas live från Avanza via Wi-Fi varje minut (grön prick = live)
3. **Foto**: `photos/pojkar.png`, konverteras med `tools/make_photo.py`

## Första start: kalibrering
Kortet behöver veta hur sensorn sitter i förhållande till skärmen. Du gör det en gång, och det sparas sedan i flashminnet.
1. Lägg kortet **platt på bordet** med skärmen uppåt och tryck **BOOT** (eller PWR).
2. Håll kortet **upprätt framför dig** (stående, skärmen mot dig) och tryck **BOOT** (eller PWR).

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
.venv/bin/pio run -e lcd169 -t upload      # bygg + flasha
.venv/bin/python -I tools/sim.py list       # testscenarier
.venv/bin/python -I tools/sim.py scenario spin shake   # kör med simulerad sensordata -> tools/out/*_sheet.png
.venv/bin/python -I tools/sim.py shot tools/out/now.png  # skärmdump av bildbufferten
```
Lägg till `-DQUIET_BUILD` i `platformio.ini` för att stänga av allt ljud medan du utvecklar.

Serial-kommandon: `sim 1|0`, `sens gx gy gz wz wmag jerk jx jy ff still`, `step N`, `dump`, `reset`, `stat`, `imu`, `dbg`, `calreset`, `calguess`, `page N`.

Moduler: `board.h` (pinnar), `imu.*` (QMI8658-tråd 200 Hz), `frame.*` (kalibrering + sensorvärden i skärmkoordinater),
`buddy.*` (fysik, humör, ritning), `sound.*` (icke-blockerande summer), `stock.*` (Avanza + Wi-Fi-tråd), `main.cpp`.
