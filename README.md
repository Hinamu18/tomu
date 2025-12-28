# Tomu

A lightweight audio player for Linux built in C. Tomu focuses on efficient memory usage and audio quality while maintaining a minimal footprint.

## Features

- **Lightweight**: Minimal dependencies and low memory footprint
- **Quality Audio**: use same quality audio if possible or use standard (for compatliblity)
- **Format Support**: Plays any audio format supported by FFmpeg (MP3, FLAC, WAV, OGG, AAC, etc.)
- **Simple**: Command-line interface - just point and play

### FFmpeg Libraries (Required)

Tomu requires FFmpeg development libraries to be installed on your system.

#### Ubuntu/Debian
```bash
sudo apt update
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev
```

#### Fedora/RHEL/CentOS
```bash
sudo dnf install epel-release -y
sudo dnf install https://download1.rpmfusion.org/free/el/rpmfusion-free-release-$(rpm -E %rhel).noarch.rpm -y
sudo dnf install ffmpeg-devel -y
```

#### Arch Linux
```bash
sudo pacman -S ffmpeg
```

## Installation

Clone the project:
```bash
git clone https://github.com/6z7y/tomu.git
cd tomu
```

install binary
```bash
make install
```
uninstall binary
```bash
make uninstall
```

## Usage

### Basic Playback
```bash
tomu /path/to/audio.mp3
```

## How It Works

Tomu uses a sophisticated multi-threaded architecture for smooth audio playback:

```

    ┌──────────────┐
    │  Audio File  │
    └──────┬───────┘
           │
           ▼
    ┌──────────────────┐
    │  FFmpeg Decoder  │ ◄── Decoder Thread
    │   (libavcodec)   │     (Reads & Decodes)
    └──────┬───────────┘
           │ Raw PCM Samples
           ▼
    ┌──────────────────┐
    │ Sample Converter │ ◄── Format Conversion
    │ (libswresample)  │     (Planar → Interleaved) if planar
    └──────┬───────────┘
           │
           ▼
    ┌────────────────────────────────┐
    │   Circular Audio Buffer         │ ◄── Thread-Safe
    │                                 │     Ring Buffer
    │  [====WRITE==><==READ===]       │     (2 seconds capacity)
    │                                 │
    │  • Mutex-protected              │
    │  • Condition variables          │
    │  • Auto-wrapping to end samples │
    └──────┬───────────────────────────┘
           │
           ▼
    ┌──────────────────┐
    │  MiniAudio API   │ ◄── Playback Thread
    │  Audio Backend   │     (Outputs to Device)
    └──────┬───────────┘
           │
           ▼
    ┌──────────────────┐
    │  Audio Hardware  │
    │   (Speakers)     │
    └──────────────────┘
```


**i made this tool for learn c and need music player less usage ram for i make many session this will take usage ram.**
``
this only beta not complete yet
``
