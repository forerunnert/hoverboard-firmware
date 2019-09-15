https://github.com/isabellesimova/HoverboardFirmware/blob/master/doc/2_STLinkSetup.md

Using ST link
=============

Now that you have the tools ready, let's probe our ST link to see what it's detecting:

$ st-info --probe

In response, we should see something like:

Found 1 stlink programmers
 serial: 563f7206513f52504832153f
openocd: "\x56\x3f\x72\x06\x51\x3f\x52\x50\x48\x32\x15\x3f"
  flash: 262144 (pagesize: 2048)
   sram: 65536
 chipid: 0x0414
  descr: F1 High-density device


using serial port
=================
screen /dev/ttyUSB0 115200
exit screen: CTRL-A CTRL \

sudo minicom --device /dev/ttyUSB0

make
====

compile:
$ make

flash:
$ st-flash --reset write build/hover.bin 0x8000000
