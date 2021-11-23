# esp32-stream-desktop

Project for TTGO T-Display that streams live video of desktop. Capable of 35+ fps @ 240x135 depending on image quality.
Left button toggles FPS counter, right button flips display. Press both to open and close the brightness menu.

At the top of client/stream_desktop.ino file set your WIFI_SSID, WIFI_PASSWORD, and HOST_IP definitions.

Server is very inefficient, I suspect that it may be a bottleneck. If you're getting poor FPS, delayed video, or stutters your PC may be unable to keep up. Increase the time on the server's screenCapIntervalServer variable.
