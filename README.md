** gif_play **

gif_play is a C command line program to play animated GIF images directly on
Linux framebuffer or SPI connected LCD display. The image decoding code is
included and it only needs my SPI_LCD library to access SPI LCD displays.

Written by Larry Bank<br>
Project started 6/19/2018<br>
bitbank@pobox.com<br>
Copyright (c) 2018 BitBank Software, Inc.<br>

Feastures:<br>
----------<br>
- Self-contained project for decoding animated GIF images<br>
- Output image to framebuffer (/dev/fbN) or LCD<br>
- Optionally center the image on the display<br>
- Run any number of loops through the image sequence<br>
- Easy to modify for embedded systems with no file system<br>

