# Anemometer Project 
Code for a WiFi sensor and communicator for a [Davis anemometer](https://www.davisinstruments.com/collections/anemometers/products/anemometer-for-vantage-pro2-vantage-pro).

This project uses a pair of RP2040's:

1. *The Communicator*: A [RP2040 Pico W](https://www.waveshare.com/raspberry-pi-pico-w.htm) connected to a [Waveshare Pico-Res Touch 3.5 inch touch LCD display](https://www.waveshare.com/wiki/Pico-ResTouch-LCD-3.5) 
2. *The Sensor*: A [Seeeduino Xiao](https://wiki.seeedstudio.com/Seeeduino-XIAO/) RP2040 which handles the real-time interrupts from the spinning wind gauge switch, and the analog sensing of the wind direction. 

The Pico W Communicator sits on a local WiFi network and will send out a sensor data on request. Additionally, the Communicator drives the display (in this case, showing wind speed and direction, amongst other things.)

The Communicator and Sensor are connected via I2C. The Communicator reads out the Sensor's I2C registers; obviously other sensors can be connected to the I2C bus as well.

The electronics are enclosed in a waterproof box with a transparent lid to see the display, powered by a local 5V/3A wall-adapter.


