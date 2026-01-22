# Getting started with development
First, follow the [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).
Next, with the zephyrproject venv activated clone the repository, cd into the folder it created, and run the following (note it may take a while for the commands to finish executing):
- `west init -l app/`
- `west update`
- `west blobs fetch hal-espressif`
Now you are good to go. Plug in your board, run `make` to build and `make flash` to flash the board.