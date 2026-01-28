# Getting started with development
First, follow the [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).
Next, with the zephyrproject venv activated clone the repository, cd into the folder it created, and run the following (note it may take a while for the commands to finish executing):
- `west init -l app/`
- `west update`
- `west blobs fetch hal-espressif`

Now you are good to go. Plug in your board, run `make menuconfig` to set the WiFi SSID then `make` to build.

Once the build is finished, run `make flash` to flsah the code and `make mon` to connect to the serial port (requires [minicom](https://salsa.debian.org/minicom-team/minicom)). 
