# TRS-80 game key usage — RetroStore catalog scan

Which keyboard keys every game in the RetroStore catalog reads, when, and what
each one does (sections 1–6), and a recommended mapping of those keys onto
TrashBoy's physical buttons, game by game (section 7).

Scan date: 2026-10-02. Catalog size on that date: 32 entries.

## How this was produced

- **Catalog and media** were pulled from the same unauthenticated API the
  firmware uses (`POST http://retrostore.org/api/listApps` and
  `/api/fetchMediaImages`, protobuf bodies as in `retrostore-c-sdk`).
- **Static analysis**: each program was disassembled (Z80) and every access
  to the keyboard matrix (`3800h–38FFh`) and every ROM keyboard call
  (`$KBCHAR 002Bh`, `$KBWAIT 0049h`, `$KBLINE 0040h`/`05D9h`) was located and
  decoded down to the individual matrix bit / character compare.
  Games that relocate themselves at start-up were analysed from a memory
  snapshot taken after relocation.
- **Dynamic confirmation**: every CMD game was run in a host harness built
  from the firmware's own Z80 core (`ptrs/z80.cpp`) and Model III ROM. At
  each stage of a game (title, prompts, gameplay) all 53 keys were pressed
  one at a time from a snapshot and compared against a no-key baseline; a
  key counts as "used" when it makes the game execute code it otherwise
  does not. Instruction screens were read out of emulated video RAM.
- The two views agree for every game unless a section says otherwise.
  Static analysis also caught keys the probe cannot reach (later game
  phases, e.g. Cosmic Fighter's `D`).

Confidence labels used below:

- **verified** — decoded from code *and* confirmed by pressing the key in
  the harness.
- **code** — decoded from code (and usually the game's own on-screen text),
  but that game state was not reached in the harness.
- **listing** — read from the BASIC listing only (the program cannot be
  launched as a CMD).

## Keyboard matrix reference

A game reads address `38xxh`; each set address bit selects a row, each data
bit is a key (1 = pressed).

| Address | bit0 | bit1 | bit2 | bit3 | bit4 | bit5 | bit6 | bit7 |
|---|---|---|---|---|---|---|---|---|
| `3801h` | @ | A | B | C | D | E | F | G |
| `3802h` | H | I | J | K | L | M | N | O |
| `3804h` | P | Q | R | S | T | U | V | W |
| `3808h` | X | Y | Z | | | | | |
| `3810h` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| `3820h` | 8 | 9 | : | ; | , | - | . | / |
| `3840h` | ENTER | CLEAR | BREAK | UP | DOWN | LEFT | RIGHT | SPACE |
| `3880h` | SHIFT (left) | SHIFT (right, M3) | | | | | | |

## 1. Catalog triage

28 of the 32 entries are games or interactive programs and are covered
below. 4 are skipped as not games.

| # | Title | Model | Media in RetroStore | Status |
|---|---|---|---|---|
| 1 | Armored Patrol | III | disk, CMD | analysed |
| 2 | Attack Force | III | disk, CMD | analysed |
| 3 | BEAST | I | disk, CMD | analysed |
| 4 | Battlestar Raven | III | disk, BASIC — **no CMD** | analysed from BASIC listing |
| 5 | Breakdown | III | disk, CMD | analysed |
| 6 | Cosmic Fighter | III | disk, CMD | analysed |
| 7 | Crazy Painter | III | disk, CMD | analysed |
| 8 | Dancing Demon | III | disk, BASIC — **no CMD** | analysed from BASIC listing (not really a game) |
| 9 | Defense Command | III | disk, CMD | analysed |
| 10 | Defuse | I | disk, CMD | analysed |
| 11 | Donkey Kong | III | disk, CMD | analysed |
| 12 | Eliminator | III | disk, CMD | analysed |
| 13 | Galaxy Invasion | III | disk, CMD | analysed |
| 14 | Kill-A-Pede | III | disk only — **no CMD** | analysed (`KILAPEDE/CMD` extracted from the disk image) |
| 15 | LDOS - Model I | I | disk | **skipped** — operating system |
| 16 | LDOS - Model III | III | disk | **skipped** — operating system |
| 17 | Meteor Mission 2 | III | disk, CMD | analysed |
| 18 | Missile Defense | III | disk, CMD | analysed |
| 19 | Rear Guard | III | disk, CMD | analysed |
| 20 | RetroStore Client | III | disk, CMD | **skipped** — store client, not a game |
| 21 | Robot Attack | III | disk, CMD | analysed |
| 22 | Sea Dragon | III | disk, CMD | analysed |
| 23 | Space Castle | III | disk, CMD | analysed |
| 24 | Space Invaders (Model I Edition) | I | disk, CMD | analysed |
| 25 | Stellar Escort | III | disk only — **no CMD** | analysed (`STELLAR/CMD` extracted from the disk image) |
| 26 | Super Nova | III | disk, CMD | analysed |
| 27 | TRS-80 Tutorial | III | 2 disks | **skipped** — LDOS tutorial image |
| 28 | Tank Zone | III | disk, CMD | analysed |
| 29 | Trek14 | III | disk, CMD | analysed |
| 30 | Vicious Vipers | III | disk, CMD | analysed |
| 31 | Weerd | III | disk, CMD | analysed |
| 32 | Zaxxon | III | disk, CMD | analysed |

The firmware only fetches COMMAND media, so the four "no CMD" titles
(Battlestar Raven, Dancing Demon, Kill-A-Pede, Stellar Escort) cannot be
launched on the device today. They are documented anyway.

## 2. The complete key list

Every key that at least one game reads, and what it does there. "Text entry"
(high-score names and similar, which accept the whole keyboard) is listed
separately in section 4.

### Keys that already have a physical button

| Key | Used for (game) |
|---|---|
| **UP** | Move up: Attack Force, Crazy Painter, Defuse, Kill-A-Pede, Missile Defense, Rear Guard, Robot Attack, Sea Dragon, Stellar Escort, Vicious Vipers, Trek14 (scan view). Climb ladder: Donkey Kong. Climb (altitude): Zaxxon. Thrust: Space Castle. Menu row: Defuse. |
| **DOWN** | Move down: same list as UP except Space Castle. Dive: Zaxxon. |
| **LEFT** | Move left: Attack Force, Breakdown, Cosmic Fighter, Crazy Painter, Defense Command, Defuse, Donkey Kong, Eliminator, Galaxy Invasion, Kill-A-Pede, Meteor Mission 2, Missile Defense, Robot Attack, Sea Dragon, Space Invaders, Stellar Escort, Vicious Vipers, Weerd, Zaxxon, Trek14. Rotate counter-clockwise: Space Castle. Menu value: Defuse. Backspace in ROM text entry. |
| **RIGHT** | Same list as LEFT, opposite direction. Rotate clockwise: Space Castle. |
| **SPACE** | Fire: Armored Patrol, Tank Zone, Attack Force, Cosmic Fighter, Defense Command, Eliminator, Galaxy Invasion, Kill-A-Pede, Missile Defense, Rear Guard, Robot Attack, Sea Dragon, Space Castle, Space Invaders, Stellar Escort, Weerd, Zaxxon. Jump: Donkey Kong. Hyperspace: Super Nova. Descent / retro rockets / missiles: Meteor Mission 2. Reveal code: Defuse. "Screen is full": Crazy Painter. Pause toggle: Vicious Vipers. Start / serve: Breakdown. Start: Space Invaders. Pass a turn: Trek14. Stop show: Dancing Demon. |
| **ENTER** | Instructions from title: Attack Force, Cosmic Fighter, Defense Command, Galaxy Invasion, Kill-A-Pede, Meteor Mission 2, Missile Defense, Super Nova, Weerd. Start / begin: Armored Patrol, Tank Zone, Crazy Painter, Defuse, Eliminator, Rear Guard, Sea Dragon, Trek14. Smart bomb: Eliminator. Vertical missile: Sea Dragon. Shield: Rear Guard. Resume from pause: Armored Patrol, Crazy Painter, Eliminator, Rear Guard, Sea Dragon, Weerd, Zaxxon. Continue / skip wait: Breakdown, Missile Defense, Space Castle. Confirm text and numbers everywhere. |
| **CLEAR** | Start game: Attack Force, Cosmic Fighter, Galaxy Invasion, Kill-A-Pede, Meteor Mission 2, Missile Defense, Super Nova. High scores: Defense Command. Directions: Crazy Painter. Hyperspace: Eliminator. "Don't save hiscores" at start-up: Weerd, Stellar Escort. Half of the abort combos (below). |
| **BREAK** | Skip intro: Attack Force, Robot Attack. Quit to title: BEAST, Kill-A-Pede, Space Invaders. Quit program: Breakdown, Space Invaders (on title). High scores: Crazy Painter. Half of the abort combos (below). |

### Keys with no physical button today

| Key | Used for (game) |
|---|---|
| **SHIFT** | High-speed movement while held: Weerd. High scores from title: Kill-A-Pede. Modifier in the pause / reset combos (below). |
| **@** | Fire: Attack Force, Defense Command, Stellar Escort. Thrust: Space Castle. With SHIFT = pause: Weerd. |
| **A** | Left tread forward: Armored Patrol, Tank Zone. Left: BEAST, Vicious Vipers. Up: Crazy Painter. Down: Eliminator, Stellar Escort. Fire: Defense Command. Attack mission: Battlestar Raven. With CLEAR = abort: Stellar Escort. |
| **B** | Fire: Defense Command. Bases: Trek14. Blasters: Battlestar Raven. Easter egg (with 5): Meteor Mission 2. |
| **C** | Fire: Defense Command. Computer: Trek14. Crew to repair: Battlestar Raven. |
| **D** | Dock & refuel: Cosmic Fighter. Right: BEAST, Vicious Vipers. Fire: Defense Command. Damage report / dock: Trek14. Dimension jump: Battlestar Raven. |
| **E** | Fire: Defense Command. Exploratory mission: Battlestar Raven. |
| **F** | Fire: Attack Force, Cosmic Fighter, Defense Command, Galaxy Invasion, Robot Attack, Weerd. Fusion drive: Battlestar Raven. |
| **G** | Start game: Stellar Escort. Fire: Defense Command. |
| **H** | Help: Vicious Vipers. Hall of fame: Stellar Escort. Hyperjump: Battlestar Raven. |
| **I** | Instructions: Armored Patrol, Tank Zone, Stellar Escort. Up: Robot Attack, Vicious Vipers. Pull up: BEAST. Also starts Eliminator from its title. |
| **J** | Left: Robot Attack, Vicious Vipers. Pull left: BEAST. |
| **K** | Right: Robot Attack. Down: Vicious Vipers. Pull down: BEAST. Klingon locations: Trek14. |
| **L** | Right: Vicious Vipers. Pull right: BEAST. Long range scan: Trek14. |
| **M** | Down: Robot Attack. Toggle M1/M3 mode on title: BEAST. |
| **N** | "No": Weerd, Stellar Escort, Trek14, Battlestar Raven, Dancing Demon. Energy scoop: Battlestar Raven. |
| **O** | Thrust: Super Nova. Left: Crazy Painter. |
| **P** | Fire: Super Nova. Right: Crazy Painter. Phasers / planets: Trek14. Pause, and "500" at the blaster prompt: Battlestar Raven. |
| **Q** | Up: Eliminator, Stellar Escort. Quit program: Space Castle (title), Crazy Painter (players prompt). Quantum torpedoes: Battlestar Raven. |
| **R** | Rotate left: Super Nova. Descent / retro rockets / missiles: Meteor Mission 2. Reverse video: Weerd. Return: Trek14, Battlestar Raven. |
| **S** | Down: BEAST, Vicious Vipers. Start: Vicious Vipers. Emergency shields, and hiscores from title: Weerd. Short range scan: Trek14. Status: Battlestar Raven. With SHIFT = pause in five games (below). |
| **T** | Rotate right: Super Nova. Torpedoes: Trek14. Toggle 7/8-bit graphics on title: BEAST. |
| **W** | Up: BEAST, Vicious Vipers. Warp: Trek14. |
| **X** | Exit program from title: Tank Zone. |
| **Y** | "Yes": Weerd, Stellar Escort, Trek14, Battlestar Raven, Dancing Demon. |
| **Z** | Left tread backward: Armored Patrol, Tank Zone. Down: Crazy Painter. Away mission: Battlestar Raven. |
| **0** | Skill "Novice": Sea Dragon. Skill: Space Castle, Crazy Painter. Bomb: Defense Command. Self destruct: Battlestar Raven. |
| **1**, **2** | Number of players: Armored Patrol, Tank Zone, Attack Force, Cosmic Fighter, Crazy Painter, Defense Command (also starts the game), Eliminator, Galaxy Invasion, Meteor Mission 2, Missile Defense, Rear Guard, Robot Attack (also starts the game), Sea Dragon, Stellar Escort, Super Nova, Weerd (also starts the game), Battlestar Raven. Skill: Sea Dragon (1 = Expert), Zaxxon, Space Castle, Crazy Painter. |
| **3**, **4** | Skill: Zaxxon (1–4), Space Castle, Crazy Painter. `4` = left: Vicious Vipers. |
| **5**, **6**, **7** | Skill: Space Castle (0–7), Crazy Painter. `5` = down, `6` = right: Vicious Vipers. `5` in the Meteor Mission 2 easter egg. |
| **8**, **9** | Skill: Crazy Painter (0–9). `8` = up: Vicious Vipers. |
| **0–9** (any) | Anti-matter bomb: Defense Command. Defuse code (only 1–8): Defuse. Numeric entry: Trek14, Dancing Demon. Menu 1–7: Dancing Demon. Commands and blaster power: Battlestar Raven. |
| **,** (comma) | Left: Cosmic Fighter, Defense Command, Galaxy Invasion, Meteor Mission 2, Stellar Escort, Weerd. |
| **.** (period) | Right: same six games. Right tread backward: Armored Patrol, Tank Zone. |
| **;** | Right tread forward: Armored Patrol, Tank Zone. |
| **/** | Instructions from title (shown as `<?>`): Space Castle. |

### Key combinations (keys that must be down at the same time)

| Combination | Function | Games |
|---|---|---|
| BREAK + CLEAR | Abort game, back to title | Attack Force, Cosmic Fighter, Crazy Painter, Defense Command, Galaxy Invasion, Meteor Mission 2, Missile Defense, Robot Attack, Space Castle, Super Nova, Tank Zone, Weerd |
| SHIFT + BREAK | Reset to title | Armored Patrol, Eliminator, Rear Guard, Sea Dragon, Tank Zone, Zaxxon |
| SHIFT + S | Pause (ENTER resumes) | Armored Patrol, Eliminator, Rear Guard, Sea Dragon, Zaxxon |
| SHIFT + @ | Pause (ENTER resumes) | Weerd |
| ENTER + CLEAR | Pause (ENTER alone resumes), undocumented | Crazy Painter |
| CLEAR + A | Abort game | Stellar Escort |
| SHIFT + CLEAR | Erase high-score table (at the players prompt) | Rear Guard |
| LEFT + RIGHT | Fire to the sides | Missile Defense |
| A + ; / Z + . / A + . / Z + ; | Forward / backward / fast right turn / fast left turn | Armored Patrol, Tank Zone |
| Fire + one direction | Shoot in that direction | Robot Attack (F or SPACE), Kill-A-Pede (SPACE while moving) |
| Two arrows | Diagonal movement | Missile Defense, Robot Attack, Sea Dragon, Zaxxon, Defuse, Crazy Painter, Stellar Escort. Not Attack Force (the most recently pressed arrow wins) and not Kill-A-Pede (two arrows match nothing). |
| Direction + SHIFT | High-speed movement | Weerd |
| B + 5 (nothing else) | Easter egg: hidden message until BREAK | Meteor Mission 2 |

## 3. Per-game reference

Addresses in the Evidence column are in the running program (after any
relocation), for anyone who wants to re-check the decode.

---

### Armored Patrol

Battlezone-style tank game. Verified. **Does not survive a ROM-less launch**
(section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | I | Instructions (2 pages) | ROM `$KBCHAR`, `CP 49h` @ `A3A0` |
| Title | ENTER | Start; also "continue" on each instruction page | `A3A5`, `A3DE` |
| "1 or 2 Players?" | 1, 2 | Number of players | ROM `$KBWAIT` @ `A40D` |
| Play | A | Left tread forward | `3801h` bit1 @ `AB39` |
| Play | Z | Left tread backward | `3808h` bit2 @ `AB49` |
| Play | ; | Right tread forward | `3820h` bit3 @ `AB41` |
| Play | . | Right tread backward | `3820h` bit6 @ `AB51` |
| Play | SPACE | Fire | `3840h` bit7 @ `A9FB` |
| Play | SHIFT + S | Pause; ENTER resumes | `A580`, `A58D` |
| Play | SHIFT + BREAK | Reset to title | `A56C` |
| High score | whole keyboard + ENTER | Name entry | ROM line input @ `BD52` |

`A`+`;` drives forward, `Z`+`.` backward, `A`+`.` turns right fast, `Z`+`;`
turns left fast; a single tread key turns slowly. Arrow keys are not read.
Also reads an Alpha joystick on port `00h`.

---

### Attack Force

Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Intro text | BREAK | Skip the intro | `3840h` bit2 @ `7FB0` |
| Title | ENTER | Instructions | `5800` |
| Title, instructions | CLEAR | Start game | `5800`, `5852` |
| "Number of players (1 or 2)" | 1, 2 | Number of players (times out to title) | `3810h` @ `58CD` |
| Play | UP, DOWN, LEFT, RIGHT | Move ship; with two arrows held the most recently pressed one wins (no diagonals) | `3840h AND 78h` @ `69E9`, `6A03` |
| Play | F, @, SPACE | Fire | `3801h AND 41h` @ `654A`, `6550` |
| Play | BREAK + CLEAR | Abort to title | `70E9` |
| Demo | any key | Back to title | `38FFh` @ `70FB` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `7273` |

Also reads a joystick on port `13h`.

---

### Battlestar Raven

BASIC text command game. **No CMD media — cannot be launched today.**
Listing only. All input is single uppercase letters or digits via `INKEY$`.

| Stage | Key | Function |
|---|---|---|
| Start | letters + ENTER | "WHAT IS YOUR NAME (MAX 10 CHARACTERS)" |
| Start | 1, 2 | "1 OR 2 PLAYER?" |
| Start | 1, 2, 3 | Difficulty level |
| Command console | S | Status |
| | C | Crew to blaster repair |
| | E | Exploratory mission |
| | A | Attack mission |
| | B | Blasters |
| | Q | Quantum torpedoes |
| | N | Energy scoop |
| | H | Hyperjump to star base |
| | D | Dimension jump |
| | R | Return to IDF HQ |
| | F | Fusion drive escape burst |
| | Z | Away mission |
| | P | Pause (shows the second player's controls) |
| Console, player 2 | 1, 2, 3, 7, 8, 9, 0 | Raven commander actions (self destruct, leave sector, dimension drive, red alert, …) |
| Blaster power | 1, 2, 3, 4, 5 (or P for 5) | Power ×100 |
| Confirmations | Y, N | Yes / no |
| Many screens | any key | Continue |

---

### BEAST

Listed as Model I; auto-detects the machine. Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | any key (including SHIFT) | Start | all 8 rows @ `5451`–`5486` |
| Title | M | Toggle Model I / Model III mode | `3802h` bit5 @ `54B7` |
| Title | T | Toggle 7-bit / 8-bit graphics | `3804h` bit4 @ `54C0` |
| Play | W, A, S, D | Move up / left / down / right (pushes blocks) | `5689`–`56A4` |
| Play | I, J, K, L | Pull a block up / left / down / right | `56A9`–`56C4` |
| Play | BREAK | Abandon game, back to title | `3840h` bit2 @ `5493` |

Arrow keys, ENTER and SPACE do nothing during play.

---

### Breakdown

Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | SPACE | Start (ENTER does not work here) | key code `20h` |
| Level intro, serve, after losing a ball | SPACE or ENTER | Continue | `CP 0Dh` / `CP 20h` @ `8A8F`, `8ADC`, `8B4C` |
| Play | LEFT, RIGHT | Move paddle | codes `CAh` / `CBh` @ `838C` |
| Anywhere | BREAK | Quit the program (`CALL 4030h`, DOS abort) | code `1Bh` @ `8387` |

Uses its own scanner (`8CB4`) that reports one key at a time, lowest matrix
row first — another held key can mask LEFT/RIGHT. Also reads an Alpha
joystick on port `00h`.

---

### Cosmic Fighter

Verified, except the docking key which is code + on-screen text.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Instructions | `3840h AND 03h` @ `6408` |
| Title, instructions | CLEAR | Start game | `6408`, `643A` |
| "Number of players (1 or 2)" | 1, 2 | Number of players (times out to title) | `3810h` @ `64B4` |
| Play | LEFT or `,` | Move left | `78AE`, `78B2` |
| Play | RIGHT or `.` | Move right | `78C9`, `78CD` |
| Play | F or SPACE | Fire | `77E3`, `77E9` |
| Docking phase (after a wave) | D | Dock with the space station and refuel — text: `Press "D" to Dock & Refuel` | `3801h AND 10h` @ `74AB` (code) |
| Play | BREAK + CLEAR | Abort to title | `7BC9` |
| Demo | any key | Back to title | `38FFh` @ `7BDB` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `7D50` |

UP, DOWN and ENTER do nothing during play. Also reads a joystick on port
`13h`.

---

### Crazy Painter

Verified. **Does not survive a ROM-less launch** (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Play | exact compare @ `75DC` |
| Title | CLEAR | Directions (3 pages; CLEAR = next page, ENTER = play) | `75B4` |
| Title | BREAK | High scores | `75E1` |
| "How many players?" | 1, 2 | Number of players | ROM `$KBWAIT` @ `7618` |
| "How many players?" | Q | Quit the program (`JP 402Dh`) | `761C` |
| "Skill Level (0 to 9)" | 0–9 | Skill, asked per player | `764A`, `7675` |
| Play | UP or A | Brush up | `7B58`, `7C92` |
| Play | DOWN or Z | Brush down | |
| Play | LEFT or O | Brush left | |
| Play | RIGHT or P | Brush right | |
| Play | SPACE | Register a full screen (finish the painting) | `7B88` |
| Play | CLEAR + BREAK | End game | `CP 06h` @ `743F` |
| Play | ENTER + CLEAR | Pause; ENTER alone resumes (undocumented) | `CP 03h` @ `7444`, `744E` |
| High score | whole keyboard + ENTER | Name entry | ROM `$KBLINE` @ `72DA` |

The letter keys are read from four matrix rows OR-ed together (`380Fh`), so
every key in the same column works too: up = A/I/Q/Y, down = B/J/R/Z,
left = G/O/W, right = @/H/P/X. Also reads an Alpha joystick on port `00h`.

---

### Dancing Demon

A music-and-dance show composer, not a game. BASIC with embedded machine
code. **No CMD media — cannot be launched today.** Listing only.

| Stage | Key | Function |
|---|---|---|
| Menu | 1 | Enter a new musical score |
| Menu | 2 | Enter a new dance routine |
| Menu | 3 | Play the new or loaded show |
| Menu | 4, 5 | Save / load a show (disk) |
| Menu | 6, 7 | Play preset show #1 / #2 |
| Before a show | digits + ENTER | Speed factor 1–255 (ENTER alone = default), number of performances |
| During a show | SPACE | Stop the show |
| Score and dance editors | A–Z | Enter notes / steps |
| Editors | SPACE | Rest |
| Editors | LEFT | Back up one |
| Editors | CLEAR | Clear |
| Editors | ENTER | Back to menu |
| Disk prompts | Y, N, digits | Confirm, routine number |

---

### Defense Command

Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | 1, 2 | Start with that many players | `3810h AND 06h` @ `4967` |
| Title | ENTER | Instructions | `4A09` |
| Title | CLEAR | High scores | `4A1A` |
| Play | LEFT or `,` | Move left | `597F` |
| Play | RIGHT or `.` | Move right | `597F` |
| Play | SPACE, or any of @ A B C D E F G | Fire (documented as "F", "@" or SPACEBAR; the whole matrix row works) | `5785`, `578A` |
| Play | any digit 0–9 | Anti-Matter Bomb (destroys all aliens on screen) | `570F` |
| Play | BREAK + CLEAR | Abort to title | `6115` |
| Demo | any key | Back to title | `6123` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `516D` |

Also reads a joystick on port `13h`.

---

### Defuse

Listed as Model I. Verified. Timing-critical: it executes code inside video
RAM and warns that it "requires an untouched Model I or an accurate
emulator".

| Stage | Key | Function | Evidence |
|---|---|---|---|
| "System Test" screen | any key | Continue | `38FFh` @ `5AF2` |
| Options menu | UP, DOWN | Choose option row (Time Limit, Hints, Time Bombs) | `5BBC`, `5BC8` |
| Options menu | LEFT, RIGHT | Change the value | `5BA4`, `5BB0` |
| Options menu | ENTER | Play | `5B96` |
| Play | UP, DOWN, LEFT, RIGHT | Move the finder crosshairs | `5FEA` |
| Play | SPACE | Reveal the defuse code when over a bomb | `3840h` bit7 @ `5E92` |
| Code entry | 1, 2, 3, 4, 5, 6, 7, 8 | Enter the defuse code (0 and 9 are not used) | `6138`, `613C` |

---

### Donkey Kong

Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| — | — | No title prompt: play starts by itself after a ~15 s intro and restarts after each death | |
| Play | UP, DOWN | Climb up / down ladders | `3840h` bits 3, 4 @ `9132`, `91AA` |
| Play | LEFT, RIGHT | Walk | bits 5, 6 @ `9273`, `9231` |
| Play | SPACE | Jump | bit 7 @ `92B7` |

Nothing else is read. Keys are sampled only about 4–8 times a second, so
very short taps can be missed.

---

### Eliminator

Defender clone. Verified. **Does not survive a ROM-less launch** past the
title (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER (or I) | Start | exact compares @ `5327`, `532F` |
| "1 or 2 Players?" | 1, 2 | Number of players | ROM `$KBWAIT` @ `536D` |
| Play | LEFT, RIGHT | Thrust / face left or right | `3840h` bits 5, 6 @ `54C1` |
| Play | Q | Up | `3804h` bit1 @ `8000` |
| Play | A | Down | `3801h` bit1 @ `55CE` |
| Play | SPACE | Fire | `5674` |
| Play | ENTER | Smart bomb | `6990` |
| Play | CLEAR | Hyperspace | `5741` |
| Play | SHIFT + S | Pause; ENTER resumes | `5406`, `5410` |
| Play | SHIFT + BREAK | Reset to title | `53F2` |
| High score | whole keyboard + ENTER | Name entry (18 characters) | ROM line input @ `7720` |

The UP and DOWN arrows are **not** read — vertical movement is only on Q
and A.

---

### Galaxy Invasion

Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Instructions | `55E3` |
| Title, instructions | CLEAR | Start game | `55E3`, `5612` |
| Players prompt | 1, 2 | Number of players | `569B` |
| Play | LEFT or `,` | Move left | `66C4`, `66CA` |
| Play | RIGHT or `.` | Move right | `66D3`, `66D7` |
| Play | F or SPACE | Fire | `664D`, `6653` |
| Play | BREAK + CLEAR | Abort to title | `6A64` |
| Demo | any key | Back to title | `6A76` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `6C26` |

Also reads a joystick on port `13h`.

---

### Kill-A-Pede

Centipede clone. **No CMD media — cannot be launched today.** Verified on
`KILAPEDE/CMD` extracted from the disk image.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | CLEAR | Begin | exact compare @ `BBDF` |
| Title | ENTER | Instructions | `BBD6` |
| Title | SHIFT (either, alone) | High scores | `BBE8` |
| Play | LEFT, RIGHT, UP, DOWN | Move ship (one direction at a time) | exact compares @ `B537`–`B544` |
| Play | SPACE | Fire; also SPACE + one direction | `B549`–`B55D` |
| Play | BREAK | Abort to title | `B532` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `B467` |

The matrix row is compared for exact values, so two arrows at once (a
diagonal) match nothing. Also reads an Alpha joystick on port `00h`.

---

### Meteor Mission 2

Verified, except the easter egg (code).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Instructions | `45A9` |
| Title | CLEAR | Start game | `45B1` |
| Players prompt | 1, 2 | Number of players | `4BF1` |
| Play | R or SPACE | One action button: begin descent, fire retro rockets, fire missiles | `3804h` bit2, `3840h` bit7 @ `537D` |
| Play | LEFT or `,` | Move left | `5DDD` |
| Play | RIGHT or `.` | Move right | `5DFB` |
| Play | BREAK + CLEAR | Abort to title | `5363` |
| Demo | any key | Back to title | `5375` |
| Any time | B + 5, and nothing else | Easter egg: hidden message until BREAK | `548E`–`54DE` (code) |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `67F1` |

Also reads a joystick on port `13h`.

---

### Missile Defense

The title screen calls it "OBSTACLE RUN". Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title (after ~30 s of animation) | ENTER | Instructions | exact compare @ `6F19` |
| Title, instructions | CLEAR | Start game | `6F1E`, `6CBE` |
| "Enter number of players (1 or 2) ?" | 1, 2 | Number of players | `3810h` @ `AB70` |
| Waits between stages | ENTER | Skip the wait | `7401`, `8215`, `A048` |
| Play | UP, DOWN, LEFT, RIGHT | Move, including diagonals | exact compares @ `8518`, `A709` |
| Play | SPACE | Fire forward | `85B0`, `A7C4` |
| Play | LEFT + RIGHT together | Fire to the sides | |
| Play | BREAK + CLEAR | Abort to title | `7684` |
| Demo | any key | End demo | all rows @ `AC2A` |

Movement compares the whole arrow row for exact values, so holding ENTER,
CLEAR or BREAK at the same time cancels movement. High-score name entry is
patched out in this build (it times out by itself), so no letters are
needed.

---

### Rear Guard

Verified. Works on the device only thanks to the Model I keyboard-driver
redirect (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Begin | `3840h` bit0 @ `4C65` |
| "1 or 2 Players?" | 1, 2 | Number of players | ROM `$KBWAIT` @ `4D2F` |
| "1 or 2 Players?" | SHIFT + CLEAR | Erase the high-score table | `4D3D` |
| Play | UP, DOWN | Move ship vertically | `518A` |
| Play | SPACE | Fire | `531A` |
| Play | ENTER | Shield (drains the "Shield:" counter while held) | `50FF` |
| Play | SHIFT + S | Pause; ENTER resumes | `4E20`, `4E2A` |
| Play | SHIFT + BREAK | Reset to title | `4E0C` |
| Demo | any key | End demo | `5E85` |
| High score | whole keyboard + ENTER | Name entry (18 characters) | ROM line input @ `5EF4` |

LEFT and RIGHT are not read. Also reads an Alpha joystick on port `00h`.

---

### Robot Attack

Berzerk clone. Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Intro (speech, animation) | BREAK | Skip to title | `69A6` |
| Title | 1, 2 | Pick number of players and start | `3810h AND 06h` @ `69B3` |
| Play | UP or I | Up | `6D93`–`6DB0` |
| Play | DOWN or M | Down | |
| Play | LEFT or J | Left | |
| Play | RIGHT or K | Right | |
| Play | SPACE or F | Fire; with a direction held, shoots that way | `6DAA`, `6DB7` |
| Play | BREAK + CLEAR | Abort to title | `6FC6` |
| Demo | any key | Back to title | `6FD5` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `799F` |

Diagonals by holding two directions. Also reads a joystick on port `F7h`.

---

### Sea Dragon

Verified. **Does not survive a ROM-less launch** past the title
(section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Begin | exact compare @ `91ED` |
| "1 or 2 Players?" | 1, 2 | Number of players | ROM `$KBWAIT` @ `921C` |
| "Skill level (0=Novice 1=Expert)" | 0, 1 | Skill, asked per player | ROM `$KBWAIT` @ `9258` |
| Play | UP, DOWN, LEFT, RIGHT | Move the submarine | `9794` |
| Play | SPACE | Fire torpedo forward | `A082` |
| Play | ENTER | Launch missile upward | `A096` |
| Play | SHIFT + S | Pause; ENTER resumes | `930B`, `9315` |
| Play | SHIFT + BREAK | Reset to title | `92F7` |

---

### Space Castle

Star Castle clone. Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | 0, 1, 2, 3, 4, 5, 6, 7 | Pick skill level and start | `3810h` @ `6AB4` |
| Title | `/` (shown as `<?>`; SHIFT is not checked) | Instructions | `3820h` bit7 @ `6A95` |
| Title | Q | Quit the program (`JP 402Dh`) | `6A9D` |
| Instructions | ENTER | Back to title | `6AEC` |
| New-ship pause | ENTER | Skip the wait | `6241` |
| Play | LEFT | Rotate counter-clockwise | `640B` |
| Play | RIGHT | Rotate clockwise | `63E9` |
| Play | UP or @ | Thrust | `625A`, `6262` |
| Play | SPACE | Fire, one shot per press | `62ED` |
| Play | CLEAR + BREAK | Abort to title | `6233`, `64F3` |

No name entry (one numeric high score).

---

### Space Invaders (Model I Edition)

Listed as Model I. Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | SPACE | Start | `55A7` |
| Title | BREAK | Exit the program (`JP 402Dh`) | `55AE` |
| Play | LEFT, RIGHT | Move cannon | `594D` |
| Play | SPACE | Fire | `594D` |
| Play | BREAK | Abandon game, back to title | `560A` |
| Game over | SPACE (release, then press) | Back to title | `6031` |

---

### Stellar Escort

**No CMD media — cannot be launched today.** Verified on `STELLAR/CMD`
extracted from the disk image.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Start-up disk check | CLEAR (or ENTER to retry) | "Don't save the hiscores" | ROM `$KBCHAR` @ `9603` |
| Scrolling title | I | Instructions | `3802h` bit1 @ `5A4D` |
| Scrolling title | H | Escort Hall of Fame | `3802h` bit0 @ `5A6B` |
| Scrolling title | G | Start game | `3801h` bit7 @ `5A5C` |
| "Press # of players, 1 or 2" | 1, 2 | Number of players | `7F04` |
| Play | LEFT or `,` | Left | `6B25` |
| Play | RIGHT or `.` | Right | `6B37` |
| Play | UP or Q | Up | `6B47` |
| Play | DOWN or A | Down | `6B5D` |
| Play | SPACE or @ | Fire | `6DC2` |
| Play | CLEAR + A | Abort game — text: `To ABORT Game Press CLEAR & A Keys Together` | `5A7A` |
| Hall of fame | CLEAR, then Y / N | Erase the table | `995A` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` |

Also reads an Alpha joystick on port `00h`.

---

### Super Nova

Asteroids clone. Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | ENTER | Instructions | `647A` |
| Title, instructions | CLEAR | Start game | `6473`, `648C` |
| Players prompt | 1, 2 | Number of players | `691B` |
| Play | R | Rotate left | `3804h` bit2 @ `7606` |
| Play | T | Rotate right | `3804h` bit4 @ `75D7` |
| Play | O | Thrust | `3802h` bit7 @ `764B` |
| Play | P | Fire | `3804h` bit0 @ `7094` |
| Play | SPACE | Hyperspace | `6FE3` |
| Play | BREAK + CLEAR | Abort to title | `7063` |
| Demo | any key | Back to title | `7072` |
| High score | letters + ENTER | Name entry | ROM `$KBCHAR` @ `7C9E` |

No arrow key is read during play. Also reads a joystick on port `13h`.

---

### Tank Zone

Same game engine as Armored Patrol. Verified. **Does not survive a ROM-less
launch** (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Title | I | Instructions (any key continues each page) | ROM `$KBCHAR` @ `C009` |
| Title | ENTER | Start | |
| Title | X | Exit the program (`JP 402Dh`) | `C00C` |
| "1 or 2 Players?" | 1, 2 | Number of players | ROM `$KBWAIT` @ `7D3F` |
| Play | A / Z | Left tread forward / backward | `8432`, `8442` |
| Play | ; / . | Right tread forward / backward | `843A`, `844A` |
| Play | SPACE | Fire | `8332` |
| Play | SHIFT + BREAK, or BREAK + CLEAR | Reset to title | `7E3C`, `C023` |
| High score | whole keyboard + ENTER | Name entry | ROM line input @ `BD97` |

Tread combinations as in Armored Patrol. No pause key and no joystick code
in this build; arrow keys are not read.

---

### Trek14

Text command strategy game. Mostly code: in the harness it never got past
"Generating Galaxy 0..." (10 emulated minutes), so the menus were decoded
from the program rather than exercised. **Does not survive a ROM-less
launch** (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Start | ENTER | Begin | |
| "Command:" | S | Short range scan | compare chain @ `5B9D` |
| | L | Long range scan | |
| | W | Warp | |
| | P | Phasers | |
| | T | Torpedoes | |
| | C | Computer | |
| | D | Damage report | |
| Computer "Operation:" | K, B, P | Klingon locations, bases, planets | `5AB6` |
| Computer "Operation:" | R or ENTER | Return | `5AE6` |
| Short-range-scan view | UP, DOWN, LEFT, RIGHT | Move the ship | `81EB`–`820C` |
| Short-range-scan view | SPACE | Pass a turn | `8215` |
| Short-range-scan view | P, T, D, S, W | Phasers, torpedoes, dock, rescan, warp | `8219`–`825F` |
| Numeric prompts (Location, Energy, Direction) | 0–9, `-`, ENTER, LEFT = backspace | Number entry | `A450` |
| End | Y, N | "Play Again (Y/N)?" | `8830` |

All input goes through the ROM (`$KBCHAR` @ `9E7D`), which returns
characters, so letters must arrive as upper case.

---

### Vicious Vipers

Snake game. Verified. **Does not survive a ROM-less launch** once play
starts (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Intro animation | any key | Skip | `38FFh` @ `8628` |
| Title | S | Start | `3804h` bit3 @ `8D3F` |
| Title | H | Help (any key returns) | `3802h` bit0 @ `8D33` |
| Play | UP, W, I or 8 | Up | `8A80`–`8AD4` |
| Play | DOWN, S, K or 5 | Down | |
| Play | LEFT, A, J or 4 | Left | |
| Play | RIGHT, D, L or 6 | Right | |
| Play | SPACE | Pause; SPACE again resumes | ROM `$KBCHAR` @ `8CD1` |
| Game over | any key | Restart | `8D09` |

---

### Weerd

Verified. **Does not survive a ROM-less launch** (section 5).

| Stage | Key | Function | Evidence |
|---|---|---|---|
| Start-up disk check | CLEAR (or ENTER to retry) | "Don't save the hiscores" | ROM `$KBCHAR` @ `535F` |
| Title | ENTER | Instructions | `6E55` |
| Title, instructions | S | Display hiscores | `6E5D`, `5E48` |
| Instructions page | 1, 2 | Begin game with that many players. Does not work from the title or the hiscores page; the instructions page also cycles in by itself after a few seconds | `5E54`, `5E62` |
| Hiscores | CLEAR, then Y / N | Erase the table | `56E1` |
| Play | LEFT or `,` | Move left | `62F8` |
| Play | RIGHT or `.` | Move right | `6328`, `6337` |
| Play | SHIFT held | High-speed movement | `631B`, `634E` |
| Play | F or SPACE | Fire | `6376` |
| Play | S | Emergency shields | `61E9` |
| Play | R | Reverse (inverse) video | `60B6` |
| Play | SHIFT + @ | Pause; ENTER resumes | `60ED`–`6104` |
| Play | BREAK + CLEAR | Start over | `60D6` |
| Demo | any key | End demo | `60E6` |
| High score | letters + ENTER | "PLEASE TYPE YOUR NAME" | ROM `$KBCHAR` @ `6757` |

Also reads an Alpha joystick on port `00h`.

---

### Zaxxon

Verified.

| Stage | Key | Function | Evidence |
|---|---|---|---|
| "SKILL LEVEL (EASY 1 - 4 HARD)?" | 1, 2, 3, 4 | Pick skill and start (there is no other start key) | `3810h` @ `4562` |
| Play | LEFT, RIGHT | Bank left / right | `4ED9` |
| Play | UP | Climb | `4F52` |
| Play | DOWN | Dive | `4F46` |
| Play | SPACE | Fire | `51A0` |
| Play | SHIFT + S | Pause; ENTER resumes | `45FC`, `4603` |
| Play | SHIFT + BREAK | Restart at the skill prompt | `45EF` |

Either SHIFT key works. Also reads an Alpha joystick on port `00h`.

## 4. Facts that cut across games

### What each game needs beyond the keys that already have buttons

Buttons today: UP, DOWN, LEFT, RIGHT, ENTER, SPACE, CLEAR, BREAK. This
table lists only keys with **no alternative** among those eight.

| Game | To get into a game | During play | Optional (pause, abort, extras) |
|---|---|---|---|
| Armored Patrol | 1 or 2 | A, Z, `;`, `.` (two at once) | I (instructions); SHIFT+S; SHIFT+BREAK |
| Attack Force | 1 or 2 | — | — |
| BEAST | — | W, A, S, D, I, J, K, L | M, T on title |
| Breakdown | — | — | — |
| Cosmic Fighter | 1 or 2 | D (dock, between waves) | — |
| Crazy Painter | 1 or 2, then a digit 0–9 | — | — |
| Defense Command | 1 or 2 | any digit (anti-matter bomb) | — |
| Defuse | — | 1–8 (defuse code) | — |
| Donkey Kong | — | — | — |
| Eliminator | 1 or 2 | Q, A (up / down) | SHIFT+S; SHIFT+BREAK |
| Galaxy Invasion | 1 or 2 | — | — |
| Kill-A-Pede | — | — | SHIFT (high scores) |
| Meteor Mission 2 | 1 or 2 | — | — |
| Missile Defense | 1 or 2 | — | — |
| Rear Guard | 1 or 2 | — | SHIFT+S; SHIFT+BREAK |
| Robot Attack | 1 or 2 | — | — |
| Sea Dragon | 1 or 2, then 0 or 1 | — | SHIFT+S; SHIFT+BREAK |
| Space Castle | a digit 0–7 | — | `/` (instructions) |
| Space Invaders | — | — | — |
| Stellar Escort | G, then 1 or 2 | — | I, H; CLEAR+A (abort) |
| Super Nova | 1 or 2 | R, T, O, P | — |
| Tank Zone | 1 or 2 | A, Z, `;`, `.` (two at once) | I (instructions); SHIFT+BREAK |
| Vicious Vipers | S | — | H (help) |
| Weerd | 1 or 2 | — | S (shields), R, SHIFT (speed); SHIFT+@ (pause) |
| Zaxxon | a digit 1–4 | — | SHIFT+S; SHIFT+BREAK |
| Trek14, Battlestar Raven, Dancing Demon | letters and digits throughout — keyboard programs | | |

### Text entry

- High-score names through the ROM (letters, ENTER, LEFT as backspace):
  Armored Patrol, Attack Force, Cosmic Fighter, Crazy Painter, Defense
  Command, Eliminator, Galaxy Invasion, Kill-A-Pede, Meteor Mission 2,
  Rear Guard, Robot Attack, Stellar Escort, Super Nova, Tank Zone, Weerd.
- No name entry at all: BEAST, Breakdown, Defuse, Donkey Kong, Missile
  Defense (patched out), Sea Dragon, Space Castle, Space Invaders, Vicious
  Vipers, Zaxxon.

### Behaviours that matter for any mapping scheme

- **Exact-match readers.** Missile Defense (movement), Kill-A-Pede
  (everything), Crazy Painter and Sea Dragon (title), Eliminator (title)
  compare a whole matrix row against one value. An extra key held in the
  same row makes the game ignore the input.
- **One-key-at-a-time scanner.** Breakdown reports only the first key it
  finds.
- **Simultaneous keys are required** by the tank games (two tread keys),
  Missile Defense (LEFT+RIGHT), and every abort / pause combo.
- **Row aliasing.** Defense Command fires on any key in the `@`–`G` row;
  Crazy Painter moves on any key in the same column as A/Z/O/P.
- **Program-exit keys.** BREAK in Breakdown, BREAK on the Space Invaders
  title, X on the Tank Zone title, Q on the Space Castle title and at the
  Crazy Painter players prompt all jump to DOS (`402Dh`/`4030h`), which
  does not exist on the device.
- **Slow polling.** Donkey Kong samples keys only 4–8 times a second.

### Joystick support already in the games

Fifteen games also read a joystick port (active-low: bit0 up, bit1 down,
bit2 left, bit3 right, bit4 fire). The firmware currently answers `FFh`
(nothing pressed) on all of them.

| Port | Games |
|---|---|
| `00h` (Alpha Products joystick) | Armored Patrol, Breakdown, Crazy Painter, Kill-A-Pede, Rear Guard, Stellar Escort, Weerd, Zaxxon (Space Castle reads it once at start-up only) |
| `13h` | Attack Force, Cosmic Fighter, Defense Command, Galaxy Invasion, Meteor Mission 2, Super Nova |
| `F7h` | Robot Attack |

### Where the catalog descriptions are wrong

Several RetroStore descriptions share one boilerplate sentence ("Use the
arrow keys to move and SPACE BAR to fire. … stopped by hitting SHIFT+S …
SHIFT+BREAK aborts"). The binaries disagree:

- **Tank Zone**: movement is A / Z / `;` / `.`, not the arrows; there is no
  SHIFT+S pause.
- **Eliminator**: only LEFT/RIGHT are arrows; up and down are Q and A.
- **Donkey Kong**: there is no SHIFT+S or SHIFT+BREAK at all.
- **Rear Guard**: only UP/DOWN move; ENTER is a shield.

## 5. Launch-environment findings

Not what was asked for, but it decides which of the above can be played at
all. The harness can launch a CMD exactly as the firmware does (zeroed RAM,
no ROM boot, jump to the entry point, plus the firmware's Model I keyboard
driver redirect).

| Result when launched the way the device does it | Games |
|---|---|
| Reaches gameplay | Attack Force, BEAST, Breakdown, Cosmic Fighter, Defense Command, Defuse, Donkey Kong, Galaxy Invasion, Meteor Mission 2, Missile Defense, Rear Guard (needs the redirect), Robot Attack, Space Castle, Space Invaders, Super Nova, Zaxxon |
| Drops into the ROM's `Cass?` prompt within seconds of launch | Armored Patrol, Crazy Painter, Tank Zone, Trek14, Weerd |
| Title works; breaks at the first ROM keyboard prompt ("1 or 2 Players?") | Eliminator (screen fills with garbage), Sea Dragon (reboots into `Cass?`) |
| Title works; drops into `Cass?` when play starts | Vicious Vipers (calls ROM `$KBCHAR` every frame) |

Seven of the eight failing games run correctly in the harness when the ROM
is allowed to boot first and initialise its RAM vectors (`4000h–407Fh`)
before the CMD is loaded; they use interrupts or ROM device calls that go
through those vectors. The eighth, Trek14, then gets as far as its mission
screen and accepts ENTER, but stays on "Generating Galaxy 0..." for at
least 10 emulated minutes — I could not tell whether it is slow or stuck.

Seven further games reach gameplay but call the ROM keyboard for high-score
name entry (Attack Force, Cosmic Fighter, Defense Command, Galaxy Invasion,
Meteor Mission 2, Robot Attack, Super Nova). That path was not exercised in
device mode, so whether name entry works there is unknown.

## 6. Limits of this scan

- Trek14's menus and Meteor Mission 2's easter egg were decoded from code
  and not reached in the harness; Cosmic Fighter's `D` likewise, though the
  game's own text confirms it.
- Battlestar Raven and Dancing Demon were read from detokenized BASIC
  listings only.
- "Used" means the game reacts to the key. Keys that only change state
  inside the ROM's keyboard driver are not counted.
- The analysis ran against the Model III ROM in this repository, as the
  device does, including for the three titles RetroStore lists as Model I.
- The catalog can change; this reflects the 32 entries served on
  2026-10-02.

## 7. Recommended button mapping

### 7.1 The buttons

Names are the constants in `firmware/main/buttons.h`; the short names in the
last column are used in the tables below.

| Constant | Pin | Where | Short |
|---|---|---|---|
| `BTN_L_DPAD_UP` / `_RIGHT` / `_DOWN` / `_LEFT` | A1 / A2 / A3 / A4 | left D-pad | L-pad |
| `BTN_R_DPAD_UP` / `_RIGHT` / `_DOWN` / `_LEFT` | B1 / B2 / B3 / B4 | right D-pad | R-pad |
| `BTN_L_ACTION_UPPER` | A5 | left, below the D-pad, upper | A5 |
| `BTN_L_ACTION_LOWER` | A6 | left, below the D-pad, lower | A6 |
| `BTN_R_ACTION_UPPER` | B5 | right, below the D-pad, upper | B5 |
| `BTN_R_ACTION_LOWER` | B6 | right, below the D-pad, lower | B6 |
| `BTN_L_SHOULDER` | A0 | back, left — not wired yet | A0 |
| `BTN_R_SHOULDER` | B0 | back, right — not wired yet | B0 |
| `BTN_MENU` | A7 | centre — leaves the game; never mapped | — |
| `BTN_OSK` | B7 | centre — on-screen keyboard; never mapped | — |

### 7.2 Rules behind the recommendations

1. **Left thumb steers on the left D-pad, right thumb works B5 and B6.**
   Everything a game needs *while playing* therefore goes on B5/B6, and the
   keys that only get a game started go on A5/A6, where the left thumb can
   reach them when it is not steering.
2. **B6 = the main action (SPACE). B5 = ENTER.** SPACE is fire, jump or the
   action key in more than twenty games. ENTER confirms and starts, and in Eliminator,
   Sea Dragon and Rear Guard it is a second fire button — so the two belong
   under the same thumb.
   This is where I depart from "SPACE on A5": with SPACE on the left, the
   thumb that fires is the thumb that steers, and those three games would
   need both thumbs for the two fire keys. B5 = ENTER stays as you have it.
3. **A5 = CLEAR, A6 = `1`.** Eighteen games want a digit before play
   starts ("1 or 2 players", or a skill level), and six of those are
   started with CLEAR first. A5 then A6 starts those; B5 then A6 starts the
   ENTER-started ones. Two players, or a skill other than the mapped one, come from the
   on-screen keyboard.
4. **The right D-pad mirrors the left** unless the game needs more keys
   than four action buttons hold. Then it becomes a second stick (BEAST,
   the tank games) or four extra keys (Defuse).
5. **BREAK gets no default button.** A7 already leaves a game, which makes
   every abort combination redundant, and BREAK alone exits the program in
   Breakdown and on the Space Invaders title. It is mapped only where it
   skips a long intro (Attack Force, Robot Attack) or returns to the game's
   own title (BEAST).
6. **Shoulders are extras.** When wired: B0 repeats the main action, so
   both thumbs can stay on the D-pads; A0 gets the game's pause combination
   where it has one. Every game below is playable without them, except
   that Defuse needs the on-screen keyboard for two digits.
7. **Chords are extras too.** "A + B" in a cell means one button holds two
   TRS-80 keys down at once. Those cells are marked *(chord)*; nothing
   essential depends on them.

Compared with what the firmware does today, the default profile keeps A5 =
CLEAR and B5 = ENTER and changes A6 from SPACE to `1` and B6 from Esc/BREAK
to SPACE. This applies only while a game is running: the firmware's own
menus still need B6 = Esc for "back".

### 7.3 Default profile

| L-pad | R-pad | A5 | A6 | B5 | B6 | A0 (optional) | B0 (optional) |
|---|---|---|---|---|---|---|---|
| UP, RIGHT, DOWN, LEFT | same as L-pad | CLEAR | `1` | ENTER | SPACE | — | SPACE |

Used as it stands by ten games: Crazy Painter, Defense Command, Donkey
Kong, Galaxy Invasion, Kill-A-Pede, Meteor Mission 2, Missile Defense, Rear
Guard, Trek14 and Zaxxon (three of them add an optional extra on A0). It is
also the fallback for any game without a profile of its own.

### 7.4 Per-game profiles

Cells that differ from the default are in **bold**. "—" means leave the
button unassigned. "arrows" means UP, RIGHT, DOWN, LEFT on the matching
directions.

| Game | L-pad | R-pad | A5 | A6 | B5 | B6 | A0 (opt.) | B0 (opt.) |
|---|---|---|---|---|---|---|---|---|
| Armored Patrol | **Up = A, Down = Z**, Left/Right — | **Up = `;`, Down = `.`**, Left/Right — | **SPACE** | `1` | ENTER | SPACE | **SHIFT + S** *(chord)* | SPACE |
| Attack Force | arrows | arrows | CLEAR | `1` | **BREAK** | SPACE | — | SPACE |
| BEAST | **Up = W, Right = D, Down = S, Left = A** | **Up = I, Right = L, Down = K, Left = J** | **M** | **T** | ENTER | SPACE | **BREAK** | — |
| Breakdown | arrows | arrows | **—** | **—** | ENTER | SPACE | — | SPACE |
| Cosmic Fighter | LEFT, RIGHT; **Up = D**; Down — | same as L-pad | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Crazy Painter | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Defense Command | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Defuse | arrows | **Up = `1`, Right = `2`, Down = `3`, Left = `4`** | **`5`** | **`6`** | ENTER | SPACE | **`7`** | **`8`** |
| Donkey Kong | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Eliminator | LEFT, RIGHT; **Up = Q, Down = A** | same as L-pad | CLEAR | `1` | ENTER | SPACE | **SHIFT + S** *(chord)* | SPACE |
| Galaxy Invasion | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Kill-A-Pede | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Meteor Mission 2 | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Missile Defense | arrows | arrows | CLEAR | `1` | ENTER | SPACE | **LEFT + RIGHT** *(chord)* | SPACE |
| Rear Guard | arrows | arrows | CLEAR | `1` | ENTER | SPACE | **SHIFT + S** *(chord)* | SPACE |
| Robot Attack | arrows | arrows, or **SPACE + direction** *(chord)* | **BREAK** | `1` | ENTER | SPACE | — | SPACE |
| Sea Dragon | arrows | arrows | **`0`** | `1` | ENTER | SPACE | **SHIFT + S** *(chord)* | SPACE |
| Space Castle | arrows | arrows | **`0`** | `1` | ENTER | SPACE | — | SPACE |
| Space Invaders | arrows | arrows | **—** | **—** | ENTER | SPACE | — | SPACE |
| Stellar Escort | arrows | arrows | CLEAR | `1` | **G** | SPACE | — | SPACE |
| Super Nova | **Left = R, Right = T, Up = O**, Down — | same as L-pad | CLEAR | `1` | **SPACE** | **P** | — | **P** |
| Tank Zone | **Up = A, Down = Z**, Left/Right — | **Up = `;`, Down = `.`**, Left/Right — | **SPACE** | `1` | ENTER | SPACE | — | SPACE |
| Trek14 | arrows | arrows | CLEAR | `1` | ENTER | SPACE | — | SPACE |
| Vicious Vipers | arrows | arrows | **S** | **H** | ENTER | SPACE | — | — |
| Weerd | LEFT, RIGHT; **Up = SHIFT, Down = S** | same as L-pad | CLEAR | `1` | ENTER | SPACE | **SHIFT + `@`** *(chord)* | SPACE |
| Zaxxon | arrows | arrows | CLEAR | `1` | ENTER | SPACE | **SHIFT + S** *(chord)* | SPACE |
| Battlestar Raven, Dancing Demon | on-screen keyboard only — no profile | | | | | | | |

How each profile plays, and what is left for the on-screen keyboard (OSK):

- **Armored Patrol, Tank Zone** — Start: B5, then A6. Each D-pad is one
  tread: both up drives forward, both down reverses, one up and one down
  turns on the spot. Fire is on A5 *and* B6 because both thumbs are busy
  driving; this is the game that gains most from the shoulder buttons.
  `I` (instructions) is on the OSK. If chords are available, a
  single-stick alternative is easier to learn: L-pad Up = A + `;`,
  Down = Z + `.`, Left = Z + `;`, Right = A + `.`, with fire on B6.
- **Attack Force** — B5 (BREAK) skips the intro, which otherwise runs for
  about a minute. Then A5, A6. ENTER only opens the instructions, so it
  moves to the OSK.
- **BEAST** — Any button starts. Left D-pad moves and pushes, right D-pad
  pulls. A5 and A6 are the two title-screen toggles (machine type, graphics
  set), needed only if the game guesses wrong.
- **Breakdown** — B6 starts and serves. A5/A6 are left empty on purpose:
  the game's scanner reports one key at a time, so a held `1` would hide
  the D-pad. Do not map BREAK (it exits the program).
- **Cosmic Fighter** — Start: A5, A6. Push Up to dock when the game says
  `Press "D" to Dock & Refuel`; D does nothing at any other time.
- **Crazy Painter** — Start: B5, then A6 twice (one player, skill 1). A5
  shows the directions. Pressing A5 and B5 together is the game's own
  pause (ENTER + CLEAR); B5 resumes. Other skill levels: OSK.
- **Defense Command** — A6 starts a one-player game and is the
  anti-matter bomb during play. A5 shows high scores from the title.
- **Defuse** — Left D-pad moves the crosshairs, B6 reveals the code, and
  the code digits are R-pad (`1`–`4`), A5/A6 (`5`, `6`) and the shoulders
  (`7`, `8`). Until the shoulders are wired, `7` and `8` come from the OSK.
- **Donkey Kong** — Only the D-pad and B6 (jump) do anything.
- **Eliminator** — Start: B5, then A6. D-pad Up/Down send Q/A because the
  game does not read the up and down arrows. B6 fires, B5 is the smart
  bomb, A5 is hyperspace.
- **Galaxy Invasion, Meteor Mission 2, Missile Defense** — Start: A5, A6.
  In Missile Defense, Left on one D-pad plus Right on the other fires
  sideways; holding A5 or B5 while steering stops the ship, because the
  game compares the arrow row for exact values.
- **Kill-A-Pede** — Start: A5. B5 shows the instructions.
- **Rear Guard** — Start: B5, then A6. B6 fires, B5 is the shield.
- **Robot Attack** — A5 (BREAK) skips the spoken intro, A6 starts. The game
  only shoots while a direction is held together with fire: hold B6 and
  press a direction. With chords, the right D-pad can do that in one press
  (Up = SPACE + UP and so on), which turns it into a twin-stick shooter.
- **Sea Dragon** — Start: B5, A6, then A5 for Novice or A6 for Expert. B6
  fires torpedoes, B5 launches the upward missile.
- **Space Castle** — A5 starts at skill 0, A6 at skill 1. Left/Right
  rotate, Up thrusts, B6 fires. `/` (instructions) and skills 2–7: OSK.
- **Space Invaders** — B6 starts and fires. Do not map BREAK (on the title
  it exits the program).
- **Stellar Escort** — A5 answers the start-up disk question, B5 (G)
  starts, A6 picks one player. `I` and `H` (instructions, hall of fame):
  OSK.
- **Super Nova** — Start: A5, A6. Left/Right rotate, Up thrusts, B6 fires,
  B5 is hyperspace. ENTER (instructions) moves to the OSK.
- **Trek14** — A text game: the D-pad, B5 and B6 cover moving the ship,
  ENTER and "pass"; every command letter and number is typed on the OSK.
- **Vicious Vipers** — A5 (S) starts, A6 (H) is help, B6 pauses and
  resumes. S is also "down" during play, so A5 will turn the snake.
- **Weerd** — A5 answers the start-up disk question, B5 opens the
  instructions page, A6 starts from there. Hold Up together with Left or
  Right for high speed; Down raises the shields. The optional pause on A0
  is resumed with B5. `R` (reverse video): OSK.
- **Zaxxon** — A6 starts at skill 1; skills 2–4: OSK. Up climbs, Down
  dives.

High-score names are typed on the on-screen keyboard in every game that
asks for one.

### 7.5 What the firmware needs for this

- A profile table keyed by the RetroStore app id (already cached as
  `cached_game_t.id`), falling back to the default profile.
- Left and right D-pad mapped independently. `BTN_MAP` in `input.cpp` now
  lists all eight D-pad buttons separately, so this is a table change.
- Buttons able to send keys that have no button today: letters, digits,
  `;` `.` `@` and SHIFT.
- Optionally, two keys per button for the cells marked *(chord)*.
- The profile active only while a game runs.

RetroStore ids of the games that have a profile:

| Game | Id |
|---|---|
| Armored Patrol | `8c028afe-96b3-11e7-a68b-5b6133ca5f0c` |
| Attack Force | `b306424c-9917-11e7-9703-2fa632107ac7` |
| BEAST | `803144a0-d914-4eb1-a48e-1e9f30f0015f` |
| Breakdown | `29b20252-680f-11e8-b4a9-1f10b5491ef5` |
| Cosmic Fighter | `57c64bba-98eb-11e7-a2a3-bfb13358f8d3` |
| Crazy Painter | `be1172b0-d197-11ed-afa1-0242ac120002` |
| Defense Command | `323989d2-9915-11e7-afe7-0ba63ee4a542` |
| Defuse | `B590F148-F215-4C99-9DBE-5ED009107AD7` |
| Donkey Kong | `a2729dec-96b3-11e7-9539-e7341c560175` |
| Eliminator | `b33f68e4-96b3-11e7-829d-cbcc93c6b12e` |
| Galaxy Invasion | `662d8362-98f0-11e7-8192-6779bb2ec146` |
| Kill-A-Pede | `b03b1bd8-5cf6-4ff5-94e3-fbfcc6aaadc9` |
| Meteor Mission 2 | `4d41c116-7273-11e9-9fdb-7f28db4c9f72` |
| Missile Defense | `c8d2fe8c-96b3-11e7-af7d-cf65dcc84910` |
| Rear Guard | `e449eb80-96b3-11e7-934e-5b52e9f61d59` |
| Robot Attack | `5fc570c6-9912-11e7-a0ec-c39ca2f76e55` |
| Sea Dragon | `f9a86dc6-96b3-11e7-94df-43fdc1dbb2c8` |
| Space Castle | `eb58d97d-4d14-4951-b54b-90e6993d7a8b` |
| Space Invaders (Model I Edition) | `259847aa-ce3a-48bb-a037-e392beb96b22` |
| Stellar Escort | `aff28d7a-21f5-459e-bfe6-627efa703b87` |
| Super Nova | `4b119ad6-728e-11e9-9990-83cb8b3c3fef` |
| Tank Zone | `0c50a600-96b4-11e7-914e-ff0b408d9f6e` |
| Trek14 | `0FA9D812-A861-45EA-AC89-D6442963EEFB` |
| Vicious Vipers | `AE20A448-9434-424E-B690-685435B70BA4` |
| Weerd | `59a9ea84-e52c-11e8-9abc-ab7e2ee8e918` |
| Zaxxon | `1ca4504c-96b4-11e7-90e8-3b06b2002ebc` |

These recommendations were checked against the decode in section 3, and
the start sequences were replayed in the harness. They have not been tried
on the device, so the ergonomics are a judgement, not a measurement.
