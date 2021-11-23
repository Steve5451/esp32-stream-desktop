# esp32-stream-desktop

Project for TTGO T-Display that streams live video of desktop. Capable of 35+ fps depending on image quality.
Left button toggles FPS counter, right button flips display. Press both to open and close the brightness menu.

In the client/stream_desktop.ino file set your SSID, PASSWORD, and host variables.

Server is very inefficient, I suspect that it may be a bottleneck. If you're getting poor FPS, delayed video, or stutters your PC may be unable to keep up. Increase the time on the server's screenCapIntervalServer variable.
