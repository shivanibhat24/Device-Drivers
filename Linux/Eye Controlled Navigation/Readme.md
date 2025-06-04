# Eye-Controlled Computer Driver

A comprehensive C-based driver that enables hands-free computer control using eye tracking and computer vision technology. This driver allows users to control mouse movements, perform clicks, and execute various gestures using only their eyes.

## 🌟 Features

### Core Functionality
- **Real-time Eye Tracking**: Advanced computer vision algorithms for precise eye detection
- **Gaze-based Mouse Control**: Move cursor naturally with eye movements
- **Gesture Recognition**: Control computer through eye gestures and blinks
- **Cross-platform Support**: Works on Windows and Linux systems
- **Calibration System**: 9-point calibration for accurate gaze mapping

### Control Methods
- **Mouse Control**: Cursor movement, left/right clicks, scrolling
- **Blink Gestures**:
  - Single blink → Left click
  - Double blink → Right click
  - Triple blink → Scroll mode toggle
- **Eye Movement Gestures**:
  - Horizontal swipes → Tab navigation
  - Vertical swipes → Page scrolling

### Advanced Features
- **Smoothing Algorithms**: Reduces cursor jitter for stable control
- **Safety Mechanisms**: Screen boundary protection and timeout systems
- **Accessibility Mode**: Enhanced features for users with disabilities
- **Configurable Settings**: Customizable sensitivity and behavior
- **Event Logging**: Comprehensive logging for debugging and analysis

## 🔧 Installation

### Prerequisites

#### Linux (Ubuntu/Debian)
```bash
# Install X11 development libraries
sudo apt-get update
sudo apt-get install libx11-dev libxtst-dev build-essential

# Optional: Install OpenCV for enhanced computer vision
sudo apt-get install libopencv-dev
```

#### Windows
- MinGW-w64 or Visual Studio with C compiler
- Windows SDK (usually included with Visual Studio)

#### Optional Dependencies
- **OpenCV 4.x**: For advanced computer vision features
- **USB Camera**: Webcam or external camera for eye tracking

### Compilation

#### Linux
```bash
# Basic compilation
gcc -o eye_control eye_control.c -lX11 -lXtst -lm

# With OpenCV support
gcc -o eye_control eye_control.c -lX11 -lXtst -lm -lopencv_core -lopencv_imgproc -lopencv_objdetect -DOPENCV_AVAILABLE
```

#### Windows
```bash
# Using MinGW
gcc -o eye_control.exe eye_control.c -luser32 -lgdi32

# Using Visual Studio
cl eye_control.c user32.lib gdi32.lib
```

### Installation Steps

1. **Clone or Download** the source code
2. **Install prerequisites** for your platform
3. **Compile** using the appropriate command above
4. **Run** the executable with appropriate permissions

```bash
# Linux: May need elevated permissions for input simulation
sudo ./eye_control

# Windows: Run as Administrator for full functionality
./eye_control.exe
```

## 🚀 Quick Start

### First Run Setup

1. **Launch the application**
   ```bash
   ./eye_control
   ```

2. **Perform calibration** (Menu option 2)
   - Look at each of the 9 calibration points
   - Blink when prompted to confirm each point
   - Wait for "Calibration completed successfully!" message

3. **Configure settings** (Menu option 3)
   - Enable/disable mouse control
   - Adjust sensitivity (1-10 scale)
   - Set smoothing level (1-10 scale)

4. **Start eye control** (Menu option 1)
   - Position yourself 18-24 inches from camera
   - Ensure good lighting on your face
   - Begin controlling with your eyes!

### Basic Usage

1. **Mouse Movement**: Look where you want the cursor to go
2. **Left Click**: Single blink
3. **Right Click**: Double blink quickly
4. **Scroll Mode**: Triple blink to toggle
5. **Tab Navigation**: Quick horizontal eye movements
6. **Page Scroll**: Quick vertical eye movements

## ⚙️ Configuration

### Settings Menu

Access via main menu option 3:

| Setting | Description | Range/Options |
|---------|-------------|---------------|
| Mouse Control | Enable/disable cursor movement | ON/OFF |
| Keyboard Control | Enable/disable keyboard shortcuts | ON/OFF |
| Gesture Control | Enable/disable eye gestures | ON/OFF |
| Sensitivity | Cursor movement sensitivity | 1-10 |
| Smoothing Level | Cursor smoothing amount | 1-10 |

### Configuration File

Settings are automatically saved to `eye_control_config.txt`:

```
mouse_control=1
keyboard_control=1
gesture_control=1
sensitivity=5
smoothing_level=3
```

### Advanced Configuration

Edit the source code constants for fine-tuning:

```c
#define BLINK_THRESHOLD 0.3          // Blink detection sensitivity
#define GAZE_SMOOTHING_FACTOR 0.7    // Gaze point smoothing
#define CLICK_HOLD_TIME 500          // Click duration (ms)
#define DOUBLE_BLINK_TIMEOUT 800     // Max time between blinks (ms)
#define SCREEN_MARGIN 50             // Screen edge buffer (pixels)
```

## 🎯 Calibration Guide

### Preparation
- Sit comfortably 18-24 inches from your camera
- Ensure even lighting on your face
- Remove glasses if they cause glare
- Position camera at eye level

### Calibration Process
1. Select "Calibrate Eye Tracking" from main menu
2. Look directly at each calibration point when it appears
3. Blink once when your gaze is steady on the point
4. Wait for confirmation before moving to next point
5. Complete all 9 points for full calibration

### Calibration Points Layout
```
1 ---- 2 ---- 3
|      |      |
4 ---- 5 ---- 6
|      |      |
7 ---- 8 ---- 9
```

### Troubleshooting Calibration
- **Point not detected**: Ensure good lighting and clear view of eyes
- **Inaccurate tracking**: Recalibrate in same position you'll use the system
- **Calibration timeout**: Check camera connection and eye visibility

## 🔍 Troubleshooting

### Common Issues

#### Eye Detection Problems
- **Symptoms**: Eyes not detected, tracking lost frequently
- **Solutions**:
  - Improve lighting conditions
  - Clean camera lens
  - Adjust camera angle and position
  - Remove reflective eyewear

#### Cursor Jitter
- **Symptoms**: Cursor moves erratically or jumps around
- **Solutions**:
  - Increase smoothing level in settings
  - Recalibrate the system
  - Reduce sensitivity setting
  - Ensure stable head position

#### Clicks Not Registering
- **Symptoms**: Blinks don't trigger clicks
- **Solutions**:
  - Adjust blink threshold in code
  - Practice deliberate, full blinks
  - Check system permissions
  - Verify gesture control is enabled

#### Performance Issues
- **Symptoms**: Slow response, high CPU usage
- **Solutions**:
  - Close other applications
  - Reduce frame rate in code
  - Check system resources
  - Update graphics drivers

### Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| "Cannot open X display" | X11 connection failed (Linux) | Check DISPLAY variable, run from terminal |
| "Failed to initialize driver" | System API access denied | Run with administrator/sudo privileges |
| "Calibration timeout" | Eyes not detected during setup | Improve lighting, check camera |
| "Config file not found" | Missing configuration | File will be created automatically |

### Debug Mode

Enable detailed logging by checking `eye_control.log`:

```bash
# View real-time logs
tail -f eye_control.log

# Check recent events
tail -20 eye_control.log
```

## 🔒 Security & Privacy

### Data Privacy
- **No data transmission**: All processing happens locally
- **No storage of video**: Only eye position data is processed
- **No biometric storage**: Calibration data can be deleted anytime

### Security Considerations
- Requires elevated permissions for input simulation
- Log files may contain usage patterns
- Disable when not in use to prevent unauthorized access

### Recommended Security Practices
- Use strong system passwords
- Log out when leaving computer unattended
- Regularly review access logs
- Keep software updated

## 🛠️ Development

### Architecture Overview

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Camera Input   │───▶│  Eye Detection   │───▶│  Gaze Tracking  │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                                                         │
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│ System Control  │◀───│     Gestures     │◀───│   Calibration   │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

### Key Components

- **Eye Detection**: Computer vision algorithms for eye tracking
- **Gaze Calculation**: Mathematical models for gaze point estimation
- **Gesture Recognition**: Pattern recognition for eye movements
- **System Interface**: OS-specific APIs for mouse/keyboard control
- **Calibration System**: Mapping between gaze and screen coordinates

## 🆘 Support

### Getting Help

- **Issues**: Open an issue on the project repository
- **Discussions**: Join community discussions for questions
- **Documentation**: Check this README and code comments
- **Logs**: Always include log files when reporting issues


### Reporting Bugs

When reporting bugs, please include:

1. **System information**: OS, version, hardware specs
2. **Steps to reproduce**: Detailed reproduction steps
3. **Expected behavior**: What should happen
4. **Actual behavior**: What actually happens
5. **Log files**: Contents of `eye_control.log`
6. **Screenshots**: If applicable

## 📊 Performance

### System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 1.5 GHz dual-core | 2.5 GHz quad-core |
| RAM | 2 GB | 4 GB |
| Camera | 640x480 @ 15fps | 1280x720 @ 30fps |
| OS | Windows 7 / Ubuntu 16.04 | Windows 10 / Ubuntu 20.04 |


---

**Made with ❤️ for accessibility and human-computer interaction**

*Last updated: June 2025*
