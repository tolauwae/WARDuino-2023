# WARDuino

Warduino is a port of the Web assembly virtual machine for the ESP32/ESP8266 under the arduino toolchain.

# Overview of the project

```
├── README.md
├── WARDuino.cpp
├── WARDuino.h
├── library.properties
├── local
│   ├── main.cpp
│   ├── makefile
│   └── warduino
├── mem.c
├── mem.h
├── util.c
└── util.h
```


# ToolChain Installation and Hello World

- Download [Arduino](https://www.arduino.cc/)

- Clone this repository under `$(HOME)/Arduino/libraries`

- Add the `esp8266` driver

  - Add the repository

    - File > Preferences > Settings > Additional board manager URLS:

      ```
      http://arduino.esp8266.com/stable/package_esp8266com_index.json
      ```

    - Or follow the steps at https://github.com/esp8266/Arduino#installing-with-boards-manager

  - Go to Tools->Boards->Boards Manager

    - Search for `esp8266` and install it (version 2.5.0)

- Restart Arduino

- Plug in the board and configure the board settings in the boards menu as follows

![Board configuration options](images/board_config.png)

- Make a new sketch with the following code:

```C
#include <WARDuino.h>


WARDuino wac;

void setup() {
  Serial.begin(115200);

}

void loop() {
  Serial.println(ESP.getFreeHeap());
  int i = wac.run_module();
  Serial.println(i,HEX);
  Serial.println("DONE");

  while(true) {
    delay(5000);
  }
}
```

- Compile and upload the sketch
  - set 115200 boud



## Run WARDuino on host machine

- Install wat2wasm from the [`wabt`](https://github.com/WebAssembly/wabt) package





## Goals

- IO is moelijk en lastig want je moet dat uit elkaar halen
- De VM laat toe eenvoudig te debuggen en veranderen
- Het gebruiken van de VM is belangrijk, dan kunnen we elke taal daar naar compileren, de bedoeling is om ook die talen makelijk te debuggen
- Gemakelijk fies overschrijven en toevoegen
- plus typesysteem dat de basis typeoperaties safe maakt
- Updates getypeert houden, int->int blijft int->int, anders gaat het niet (bonus: wel als nergens gebruikt)


# LICENCE

MPL-2.0 (see LICENSE).

WARDuino by Robbert Gurdeep Singh, Christophe Scholliers and Joel Martin is licensed under a Mozilla Public License 2.0.
This is a derivative work of [kanaka/wac](https://github.com/kanaka/wac) by Joel Martin.

If you need to cite WARDuino in your research, use:

```b
@inproceedings{WARDuino2019,
 author = {Gurdeep Singh, Robbert and Scholliers, Christophe},
 title = {WARDuino: A Dynamic WebAssembly Virtual Machine for Programming Microcontrollers},
 booktitle = {Proceedings of the 16th ACM SIGPLAN International Conference on Managed Programming Languages and Runtimes},
 series = {MPLR 2019},
 year = {2019},
 isbn = {978-1-4503-6977-0},
 location = {Athens, Greece},
 pages = {27--36},
 numpages = {10},
 url = {http://doi.acm.org/10.1145/3357390.3361029},
 doi = {10.1145/3357390.3361029},
 acmid = {3361029},
 publisher = {ACM},
 address = {New York, NY, USA},
 keywords = {Arduino, Live Code Updates, Virtual Machine, WebAssembly},
}
```
