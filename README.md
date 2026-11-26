# µStreamer_rtp

**A custom fork of [µStreamer](https://github.com/pikvm/ustreamer)** — Lightweight and fast MJPEG-HTTP streamer, extended with **RTP Multicast** support.

> **Note:** This project is based on the excellent work of the original µStreamer project by Maxim Devaev. This specific fork adds the ability to stream video over RTP Multicast, in addition to the standard MJPEG-HTTP features.

---

## 🎥 RTP Multicast Support (New Feature)

This fork introduces RTP Multicast streaming. It allows you to push the video stream directly to a multicast group, which can be received by VLC or other media players on the network.

### Tested Hardware
This modification has been successfully tested and confirmed working on the **Allwinner H3 Linux Board**.

### Usage
Here is the exact command used to start the streamer with RTP multicast enabled on the Allwinner H3 board:

    ./ustreamer.bin \
      --device=/dev/video1 \
      --resolution=1920x1080 \
      --format=MJPEG \
      --desired-fps=25 \
      --host=0.0.0.0 \
      --port=8080 \
      --rtp-multicast=239.10.10.10:5004 \
      --rtp-ttl=1

*(Note: The `--host` and `--port` flags still serve the standard HTTP MJPEG stream. The new flags `--rtp-multicast` and `--rtp-ttl` control the RTP output.)*

### Playing the RTP Stream in VLC
To view the stream over RTP, you need an SDP file. A sample file named **`camera.sdp`** is included in the root of this repository.

1. Ensure `ustreamer_rtp` is running (using the command above).
2. Open **VLC Media Player**.
3. Go to **Media → Open File** and select `camera.sdp`.
4. The stream should start playing.

---

## 🛠️ Installation & Building

To build this fork, you need the standard build tools (`make`, `gcc`, `pkg-config`) and the required libraries (like `libevent` and `libjpeg`).

### 1. Clone the repository

    git clone https://github.com/FAVAPAK/ustreamer_rtp.git
    cd ustreamer_rtp

### 2. Install Dependencies
*   **Debian/Ubuntu:** `sudo apt install build-essential libevent-dev libjpeg-dev libbsd-dev`
*   **Arch Linux:** `sudo pacman -S libevent libjpeg-turbo libutil-linux libbsd`
*   **Raspberry Pi OS:** `sudo apt install libevent-dev libjpeg62-turbo libbsd-dev`

### 3. Build

    make

After a successful build, the executable will be available as `./ustreamer.bin`.

---

## 📖 Original Features
Since this is a fork, it retains all the original capabilities of µStreamer, including:
*   Multithreaded JPEG encoding.
*   Hardware image encoding on Raspberry Pi.
*   Support for DV-timings (changing resolution on the fly).
*   Ability to serve files and stream via UNIX domain sockets.
*   Compatibility with mjpg-streamer's API.

For detailed information about the original features and advanced usage, please refer to the original repository: [pikvm/ustreamer](https://github.com/pikvm/ustreamer).

---

## 📜 License & Credits

This project is a fork of [pikvm/ustreamer](https://github.com/pikvm/ustreamer).

**Original Author:** Copyright (C) 2018-2024 by Maxim Devaev (mdevaev@gmail.com)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.