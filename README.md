# KeyFinder

A project developed as part of an electronic and software design course.

The goal was to fully design a connected keychain, from the mechanical enclosure and electronic PCB to the embedded firmware.

## Overview

The project includes:

* Mechanical design of the enclosure and keychain using Fusion 360
* Electronic schematic and PCB design using KiCad
* Development of a Bluetooth Low Energy (BLE) firmware using Zephyr
* Control of a physical button and multiple LEDs
* Communication with a Bluetooth device

The mechanical model was designed manually from scratch during the course.

## Prototype Features

The current firmware provides the following features:

* Automatically blink an LED
* Control an LED using a physical button
* Control an LED remotely via Bluetooth
* Establish a Bluetooth Low Energy connection
* Simulate a battery measurement
* Resume Bluetooth advertising after disconnection

## Repository Structure

```text
hardware/
├── KeyFinder.kicad_sch
├── nrf52832_qfax.kicad_sch
├── nrf52832_qfax.kicad_pcb
├── nrf52832_qfax.kicad_pro
└── nrf52832_qfax-backups/

software/
├── peripheral_hr_button/
│   ├── src/
│   │   └── main.c
│   ├── boards/
│   ├── prj.conf
│   ├── CMakeLists.txt
│   └── README.rst
└── Projet fusion/
    └── Fusion 360 mechanical model
```
