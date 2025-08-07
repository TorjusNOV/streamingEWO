# streamingEWO

A Qt-based custom widget for WinCC OA that displays video streams with status and debug overlays.
Is used in conjunction with the [StreamServer](https://github.com/TorjusNOV/StreamServer) manager for OA.
## Features
- Connects to a video stream via WebSocket and controls RTSP stream selection.
- Displays the current video frame, maintaining aspect ratio and filling unused space with black.
- Status overlay for connection, latency, and error states, with all messages centralized for maintainability.
- Two debug modes:
  - **Debug Print**: Enables detailed runtime logging to the console (`qDebug()`).
  - **Debug Mode**: Shows a debug overlay in the widget with delay, server/client timestamps, server IP, and RTSP URL.
- Optimized for low CPU/memory usage and maintainable, modern C++/Qt code.

## Usage
1. **Build**
   - Use CMake to generate build files and compile.
   - Qt 6 binaries must be available
   - Only tested using VS 17 2022.
2. **Integration**
   - In CMakeLists.txt, change which path the resulting widget executable should be placed, e.g. "C:/WinCC_OA_Proj/RTX/bin/widgets/windows-64" 
   - Use the provided methods to set WebSocket and RTSP URLs, and toggle debug modes.
   - Make sure that the StreamServer manager (WCCOAstreamServer.exe) is running in OA's process monitor 

## Exposed Methods
- `setWebSocketUrl(string url)` — Set the WebSocket server URL.
- `setRtspStreamUrl(string url)` — Set the RTSP stream URL.
- `setDebugMode(bool enabled)` — Show/hide the debug overlay in the widget.
- `setDebugPrint(bool enabled)` — Enable/disable debug prints to the console.
