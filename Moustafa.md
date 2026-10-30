
# Team Project: ECE3730_Team_1_LoRa Rangers

## Project Overview
A fun team project with a search-and-rescue connection

## Team Members
| Name | GitHub Username | Responsibility |
|------|-----------------|----------------|
| [Moustafa Bendari] | [Moustafa-Bendari] | [website to display data/show sensor info] |
| [Anthony] | [angottianthony] | [Module or task] |
## Individual Contribution: [Your Name]

### My Responsibility
I am developing the web dashboard on the base station ESP32. It connects to WiFi, hosts a web page, and shows the latest data received from each LoRa node: node ID, sensor readings, signal strength (RSSI), and time since last update. The page refreshes automatically so new packets appear without reloading.

### My Files
- `WebDashboard.h` — Class declaration for the dashboard: WiFi setup, web server, and a function to update the stored node data.
- `WebDashboard.cpp` — Implementation: connects to WiFi, serves the HTML page at `/`, serves the latest readings as JSON at `/data`, and stores incoming packets.

### My Inputs and Outputs
| Direction | Data | Type / Units / Format | Source or Destination |
|-----------|------|-----------------------|-----------------------|
| Input | Sensor data from LoRa nodes | Numbers (node ID, readings, signal strength) | LoRa receiver |
| Output | Live dashboard | Web page | User's browser |

### My Test Plan
Send a fake reading (Node 1, temperature 24.5 °C) to the dashboard code.
**Expected result:** the web page shows Node 1 with 24.5 °C.

