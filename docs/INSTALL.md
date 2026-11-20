# Install Orbita R36S/H on ArkOS

**English** · [Português brasileiro](INSTALL.pt-BR.md)

This guide uses the **0.2.3 Linux ARM64 package**. Xbox Cloud Gaming gameplay
has been demonstrated on the R36S/H; the project remains experimental, and
performance depends on the device, ArkOS image and connection. Version 0.2.3
adds game search with an on-screen keyboard.

## What you need

- An R36S/H running ARM64 ArkOS with a writable games card.
- Internet working in ArkOS, using a compatible USB adapter if needed.
- A Microsoft account with access to the game through Xbox Cloud Gaming.
- A phone or computer to authorize Microsoft sign-in.
- A computer and card reader to copy the files.

The package uses the device's SDL, FFmpeg and Opus libraries. Different handheld
revisions and ArkOS images may need adjustments. When PortMaster is available,
the launcher reuses its helpers and controller mappings. You do not need to
compile the project, create an Orbita website account or use a beta key.

## 1. Download the right package

Open the project's [Releases](https://github.com/D1Alexandre/orbita-R36SH/releases)
and find **v0.2.3**. Under **Assets**, download:

- `orbita-r36sh-0.2.3.zip`: the ready-to-install application.
- `orbita-r36sh-0.2.3.zip.sha256`: an optional download verification hash.

Use the application ZIP. The automatic **Source code (zip/tar.gz)** files contain
source code and are not the installation package.

To verify the download in PowerShell, open the folder containing both files:

```powershell
Get-FileHash .\orbita-r36sh-0.2.3.zip -Algorithm SHA256
Get-Content .\orbita-r36sh-0.2.3.zip.sha256
```

The hashes must match, ignoring letter case.

## 2. Copy the files to the games card

1. Turn the handheld off and insert the games card into your computer's reader.
2. Open the games partition, usually named `EASYROMS`. If Windows asks to format
   a Linux partition, cancel that prompt.
3. Extract the ZIP on your computer. It contains `Orbita.sh` and an `orbita/` directory.
4. Copy **both** into the games partition's `ports` directory. Avoid creating an
   extra directory between `ports` and `Orbita.sh`.
5. Safely eject the card, put it back in the handheld and turn the device on.

The resulting layout should be:

```text
EASYROMS/
└── ports/
    ├── Orbita.sh
    └── orbita/
        ├── bin/
        │   ├── orbita
        │   └── orbita-video-test
        ├── INSTALL.md
        └── ...
```

In ArkOS, this directory usually corresponds to `/roms/ports/` or
`/roms2/ports/`, depending on the active games card.

## 3. Launch and sign in

1. Confirm that internet works in ArkOS and the system date and time are correct.
2. In EmulationStation, open **Ports** and select **Orbita**.
3. The application displays a Microsoft address and authorization code.
4. Visit the address on your phone or PC, sign in to your account and enter the code.
5. Wait for the catalog. Resolving game names may take longer on the first launch.
6. Select a game and press **A** to start it. Queue times depend on the service.

Sign-in is saved locally for later launches. Do not share authorization codes
or account files.

## Main controls

| Action | Control |
|---|---|
| Navigate the catalog | Up / down |
| Skip 6 games | Left / right |
| Skip 50 games | L1 / R1 |
| Start or resume a game | A |
| Open search | X |
| Refresh the catalog | Start |
| Find Fortnite, if present in the catalog | Select |
| Clear search; exit from the full list | B |
| Delete saved sign-in and switch accounts | Y |
| Cancel queueing or connecting | B |
| End gameplay and return to the catalog | Hold Select + Start for 0.7 s |

In search, use the directional pad to select letters and **A** to type. **X**
deletes a letter, **Y** clears the text, **Start** shows results and **B** cancels
editing. A USB keyboard also works. Search ignores case, accents, spaces and
punctuation: for example, `hotwheels` finds `HOT WHEELS`.

During gameplay, **B** is forwarded to the game normally. Use **Select + Start**
to leave the stream.

## Update an existing installation

Close the application and turn the handheld off before removing the card. Keep
a copy of `Orbita.sh` and the `orbita/` directory outside `ports` so you can return
to the previous version. Copy the new launcher and directory to the same locations.

By default, sign-in is stored in the Linux user's data directory, separately
from the package; replacing application files does not require deleting it.

## Troubleshooting

| Symptom | What to check |
|---|---|
| Orbita does not appear in Ports | Check the layout above and the active games card; restart the frontend. |
| Opens and immediately closes | Read `ports/orbita/logs/last-run.log`; check missing files, permissions and libraries. |
| Sign-in or connection fails | Check internet, date and time; request a fresh sign-in code if the old one expired. |
| IDs appear instead of names | Wait for the name cache or try refreshing the catalog; network failures can delay name lookup. |
| A listed game returns `NoEntitlement` | Try the same game on xbox.com/play with the same account; catalog visibility does not grant access. |
| Fortnite does not start | The shortcut only finds the game. The tested account received `NoEntitlement` / `OfferingAccessDenied`; launch remains unvalidated. |
| Video stalls or feels delayed | Check the connection; report the game, ArkOS version and relevant `STAT` log lines to help diagnosis. |

If executable permissions need fixing, run this in the handheld's terminal
from the correct `ports` directory:

```sh
chmod +x Orbita.sh orbita/bin/orbita orbita/bin/orbita-video-test
```

To inspect the executable and its dependencies on the device:

```sh
./orbita/bin/orbita --diagnose
ldd orbita/bin/orbita
```

Do not replace system glibc or graphics drivers to force the application to run.
When reporting a problem in [Issues](https://github.com/D1Alexandre/orbita-R36SH/issues),
include the model/revision, ArkOS image, Orbita version, connection, game and
reproduction steps. Review log excerpts and remove account data before posting.
