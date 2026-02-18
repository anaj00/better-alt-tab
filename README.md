# Better Alt+Tab

A sleek, minimal window switcher for Windows that replaces the default Alt+Tab with a modern, Raycast-inspired interface.

## Features

✨ **Minimalist Design**

- Clean, dark UI with Segoe UI typography
- Compact window list (28px rows)
- Smooth acrylic blur effect (Windows 10/11)
- No clutter, just window titles

🚀 **Lightweight & Fast**

- Single executable, no dependencies
- Instant window enumeration
- Low memory footprint
- Filters out system UI windows automatically

⌨️ **Keyboard Shortcuts**

- **Alt+Tab** - Open switcher
- **Alt+Shift+Tab** - Cycle backwards
- **Up/Down arrows** - Navigate
- **Enter** - Switch to selected window
- **Escape** - Cancel
- **Double-click** - Switch to window

🔧 **Smart Features**

- Auto-hides when focus is lost
- Filters cloaked/hidden Windows 11 UI windows
- Bypasses TextInput popup and system utilities
- Runs in system tray
- Optional startup registration

## Installation

### Option 1: Direct Download (Easiest)

1. Download `switcher.exe` from [Releases](https://github.com/anaj00/minimalist-alt-tab/releases)
2. Run the `.exe` file
3. Done! It will live in your system tray

### Option 2: Run at Startup

1. Launch the app
2. Right-click the tray icon
3. Click "Run at startup"
4. The switcher will now launch automatically on boot

## Build from Source

### Requirements

- Windows 10/11
- GCC (MinGW) or MSVC
- Standard Windows SDK

### Compile

```bash
g++ switcher.cpp -o switcher.exe -luser32 -lgdi32 -lcomctl32 -ldwmapi -lshell32 -mwindows
```

Or with MSVC:

```bash
cl switcher.cpp user32.lib gdi32.lib comctl32.lib dwmapi.lib shell32.lib
```

## Usage

**Launch the app** - It will sit in your system tray (bottom-right)

**Use Alt+Tab** - Opens the window switcher

- Navigate with arrow keys
- Select with Enter
- Cancel with Escape

**Right-click tray icon** for options:

- Enable/disable startup
- Exit the application

## Customization

Edit these constants in `switcher.cpp` to customize:

```cpp
#define WIN_W       520         // Window width (pixels)
#define PADDING_V   0           // Vertical padding
#define ROUND_RAD   14          // Corner radius
#define ITEM_H      28          // Row height (pixels)
#define TITLE_FONT_SIZE 15      // Font size
#define BG_COLOR    RGB(20,20,22)      // Background color
#define SEL_BG_COLOR RGB(50,50,65)     // Selection highlight color
#define TITLE_COLOR RGB(250,250,255)   // Text color
```

Then recompile with: `g++ switcher.cpp -o switcher.exe -luser32 -lgdi32 -lcomctl32 -ldwmapi -lshell32 -mwindows`

## System Requirements

- Windows 10 or later
- No additional dependencies (all libraries are system-provided)
