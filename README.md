# Anemometer Project 
Code for a WiFi sensor and communicator for a [Davis anemometer](https://www.davisinstruments.com/collections/anemometers/products/anemometer-for-vantage-pro2-vantage-pro).

This project uses a Pico W board [RP2040 Pico W](https://www.waveshare.com/raspberry-pi-pico-w.htm) connected to a [Waveshare Pico-Res Touch 3.5 inch touch LCD display](https://www.waveshare.com/wiki/Pico-ResTouch-LCD-3.5) 

The Pico W is ridiculously powerful for a simple WiFi-connected data logger. It can easily service real-time-ish interrupts while communicating over UDP with some other computer (as well as using the I2C bus.)

The electronics are enclosed in a waterproof box with a transparent lid to see the display, powered by a local 5V/3A wall-adapter.

The layout and circuit for the glue board are also included here. There are provisions for I2C devices, a Davis Anemometer, and a 5V servo motor (such as the MG 996R.) There are also a couple of jumpers that can be configured in "debug mode" where the Pico itself simulates the signals that should come from the anemometer (basically, the wind direction 0-3.3 V analog level, and the wind speed reed switch which produces digital pulses at 0 - ~1kHz.)  


