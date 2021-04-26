# nx-btred
nx-btred is a Bluetooth audio driver/redirector for Switch.

It uses the audrec:u service to record game audio, and then outputs it on the new audio bluetooth driver introduced in firmware version 12.0.0.

## Installation
1. Install firmware 12.0.0+.
2. Install latest [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere/releases/).
3. Install latest [MissionControl](https://github.com/ndeadly/MissionControl/releases/tag/v0.5.0-alpha).
4. Download nx-btred and unzip to your SD card.

## Usage
1. Enter the homebrew menu.
2. Launch the btpair application.
3. Press X to scan.
4. Select your headphones and click A.
5. Wait for it to pair.
6. Enjoy!
7. If the device connects but no audio output, press the power button to enter sleep-mode, then press it again to wake up.

## Limitations
Due to a limitation of the audrec:u service, only games audio can be recorded (not the system applets).

## Thanks
Thanks to ndeadly, SciresM and the Switchbrew crowd