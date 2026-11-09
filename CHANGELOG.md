# Changelog

Each release's notes on GitHub are taken from its section here.

## 1.0.4

- **Fixed Spectra not loading on some Windows computers.** The plugin needed Microsoft's Visual C++ files to be already installed, and on computers without them it failed to load without saying anything — it installed fine but never appeared in the plugin list. Everything it needs is now built in.

## 1.0.3

- **Fixed a loud jump on Sub Drone.** Shifting the pitch down made the pad much louder (about 16 dB on Sub Drone), and shifting up made it quieter. Shift now keeps the level steady.
- **Tone no longer changes the volume.** It now only makes the pad darker or brighter.
- Every factory preset now plays at a similar loudness, and a new automated test keeps it that way.

## 1.0.2

- **Save your own presets.** On the presets page, click *save as...*, type a name, and press Enter. Your presets appear under *your presets*. Right-click one to delete it or show it in Finder or Explorer.
- **Audio Unit version for Logic Pro and GarageBand.** The Mac installer now installs both the VST3 and the Audio Unit.
- Spectra is now open source under the GNU AGPLv3 license.

## 1.0.1

- New **presets** page: every preset with a short description. Click a preset name at the top to open it.
- The window-size menu now matches the plugin's look.
- Hover descriptions are easier to read and no longer squeezed.

## 1.0.0

- First release: spectral freeze with size, diffuse, shift and mix on the main page, eleven more controls on the settings page, and eight factory presets.
- Installers for Mac (Apple Silicon and Intel) and Windows.
