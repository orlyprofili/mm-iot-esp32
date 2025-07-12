# HaLow OSC + iperf Demo

A demonstration project combining network performance testing (iperf) with real-time OSC (Open Sound Control) beacon transmission over Morse Micro's HaLow (802.11ah) wireless technology.

## Overview

This project extends the standard iperf example to broadcast OSC messages at 1 Hz, creating a triangle wave LFO (Low Frequency Oscillator) that can be monitored by any OSC-compatible application on the network. It's designed for the ESP32-S3 with MM6108 HaLow module.

## Features

- **Network Performance Testing**: Full iperf functionality (TCP/UDP server/client modes)
- **OSC Beacon**: Broadcasts triangle wave LFO values (0.0 → 1.0 → 0.0) at 1 Hz
- **HaLow Connectivity**: Utilizes 802.11ah for long-range, low-power wireless communication
- **Real-time Monitoring**: OSC messages can be captured and visualized by standard OSC tools

## Hardware Requirements

- ESP32-S3 development board
- MM6108 HaLow wireless module
- Appropriate antenna for sub-GHz operation
- Morse HaLow Openwrt router, in this case rpi4+seeed pcie hat+seeed pcie halow module

## Software Requirements

### Development Environment

- ESP-IDF framework
- Morse Micro HaLow SDK

### Monitoring Tools (macOS)

```bash
# Install OSC tools (one-time setup)
brew install liblo
```

## Building and Flashing

1. **Configure the project**:

   ```bash
   idf.py menuconfig
   ```

2. **Build the project**:

   ```bash
   idf.py build
   ```

3. **Flash to ESP32-S3**:

   ```bash
   idf.py flash monitor
   ```

## Setting up router

Go to halow_backups/ and use an llm to restore the openwrt router configuration. 
Ask it to make a shell script for future use.  There's one to backup configurations currently.


## Usage

### OSC Monitoring

After flashing and connecting to your HaLow network, monitor the OSC beacon:

```bash
# Listen for OSC messages on port 8000
oscdump 8000

```

**Expected Output:**

```
/lfo f 0.00
/lfo f 0.02
/lfo f 0.04
...
/lfo f 0.98
/lfo f 1.00
/lfo f 0.98
...
```

### iperf Testing

The demo supports all standard iperf modes:

#### UDP Server (Default)

```bash
# On your computer, run iperf client
iperf -c <ESP32_IP> -u -p 5001
```

#### TCP Server

```bash
# Configure for TCP server mode and rebuild
# Then run iperf client
iperf -c <ESP32_IP> -p 5001
```

#### Client Modes

Configure the target server IP in the code and rebuild.

## Configuration

### OSC Settings

```c
#define OSC_DEST_IP "255.255.255.255"  // Broadcast address
#define OSC_DEST_PORT 8000             // OSC port
#define OSC_TASK_PRIORITY MMOSAL_TASK_PRI_LOW
```

### iperf Settings

```c
#define IPERF_TYPE IPERF_UDP_SERVER    // Server/client mode
#define IPERF_SERVER_IP "10.0.0.15"    // Target IP for client mode
#define IPERF_SERVER_PORT 5001          // iperf port
#define IPERF_TIME_AMOUNT -10           // Test duration (seconds)
```

## OSC Message Format

The demo broadcasts standard OSC messages with the following structure:

- **Address**: `/lfo`
- **Type**: `f` (32-bit float)
- **Value**: Triangle wave (0.0 to 1.0)
- **Frequency**: 1 Hz (100 steps × 10ms intervals)

## Network Architecture

```
┌─────────────────┐    HaLow     ┌──────────────────┐
│   ESP32-S3      │◄────────────►│   Access Point   │
│   + MM6108      │   802.11ah   │                  │
│                 │              │                  │
│ ┌─────────────┐ │              │ ┌──────────────┐ │
│ │ OSC Beacon  │ │              │ │   Network    │ │
│ │   1 Hz      │ │              │ │   Bridge     │ │
│ └─────────────┘ │              │ └──────────────┘ │
│ ┌─────────────┐ │              └──────────────────┘
│ │ iperf       │ │                       │
│ │ Server      │ │                       │ Ethernet/WiFi
│ └─────────────┘ │                       │
└─────────────────┘                       ▼
                                  ┌──────────────────┐
                                  │   Host Computer  │
                                  │                  │
                                  │ ┌──────────────┐ │
                                  │ │ OSC Monitor  │ │
                                  │ │ (oscdump)    │ │
                                  │ └──────────────┘ │
                                  │ ┌──────────────┐ │
                                  │ │ iperf Client │ │
                                  │ └──────────────┘ │
                                  └──────────────────┘
```

## Troubleshooting

### No Ping

In two terminals, ssh into the openwrt router.
ssh root@<ipaddress>

and run this (one in each terminal):

```bash
tcpdump -i wlan0 -nn -v -xx ether host 2c:c6:82:8a:2a:b6 and icmp

tcpdump -i wlan0 -nnvv -xx ether host 2c:c6:82:8a:2a:b6 and icmp
```

Then ping it from your computer, then llm your way out of it. 
It's not consistent or clear to what has made iperf and ping work or not work, but this is a good start.


### No OSC Messages Received

- Verify network connectivity between ESP32 and monitoring host
- Check firewall settings on the monitoring host
- Ensure the broadcast address is correct for your network
- Try listening on `0.0.0.0:8000` instead of broadcast address

### iperf Connection Issues

- Verify IP addresses and port configurations
- Check that the ESP32 is connected to the HaLow network
- Ensure no firewall blocking on the specified ports

### Build Errors

- Verify ESP-IDF and Morse Micro SDK are properly installed
- Check that all required components are available
- Ensure the target is set to ESP32-S3

## Development Notes

- The OSC task runs at low priority to avoid interfering with network operations
- Triangle wave generation uses simple phase accumulation for smooth transitions
- OSC packets are manually constructed for minimal overhead
- The demo uses broadcast UDP for maximum compatibility with OSC tools

## License

Apache-2.0

## Contributing

This is a demonstration project. For production use, consider:

- Error handling and recovery mechanisms
- Configurable OSC parameters via menu system
- Multiple OSC parameter transmission
- Integration with audio/sensor data sources
