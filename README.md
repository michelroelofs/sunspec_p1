# powermonitor

PI P1 powermonitor
==================

Compiling
---------

make



RPI configuration
-----------------

Run the raspi-config tool, to disable the serial console: select 5 interfacing
options > 6 serial to disable shell and kernel messages on serial connection.

Add the powermonitor.service to /etc/systemd/system

SERIAL PORT PINOUT
------------------

1 dcd
2 rx
3 tx
4 dtr
5 gnd
6 dsr
7 rts
8 cts
9 nc

P1 PORT PINOUT
--------------

From https://developers.nl/blog/10/uitlezen-nederlandse-slimme-meter-p1-poort-over-netwerk
2 RTS
5 TX
3 GND

Pins: lip of connector down, contacts up, left to right: 6 5 4 3 2 1

Connecting P1 to RS232, assuming the RS232 can just read the data
RS232-DB9   P1-RJ12

   5           3
   2           5
   7           2

A complete schematic can be:

        7 -->|-------+--- 2
                     |
        2 --+---R1k--+
            |------------ 5

        5 --------------- 3

The diode between DB9/7 and RJ12/2 is simply to ensure the pullup/activation
works fine on pin 2, and also works as a pullup for DB9/2.

P1 data from TCP
----------------

Instead of reading the data from a serial port, the data can also be read from
a TCP stream. Use the bash method to specify a hostname/portnumber:
/dev/tcp/hostname/portnumber.

SMARTMETER FIELDS
-----------------

Most fields are well explained in the documentation. However, the ampere
fields, e.g. 1-0:71.7.0.255, just show the value absolute current, the phase
relative to the voltage is not taken into account. This was proven by an
experiment.

Experiment Ampere Meter
-----------------------

Setup: The solar panels are producing 5.2 kW, according to the ammeter 7A in
each phase. Switching on the left stove at full power and the oven, both ~
15A, made the current change to 22A. So the consumers are ~ 32A, the producer
7A, 22A+7A=29A, so we still miss 3A. But this may be because of inaccuracies
in the measurements.

Modbus reading
==============

Next to reading the data from the P1 port of the Dutch smart meter, also
reading Sunspec data by modbus over TCP is supported. Using a timer, every
second a request is sent over modbus to the specified sunspec device, and its
data is captured.

Using libmodbus was considered, but eventually replaced by sending a simple
hand-crafted command and reading the data directly. This is because libmodbus
doesn't support event-driven communication using select(2) or poll(2), and the
actual modbus interface was simple enough.

Interfacing
===========

The tool supports both a UDP and TCP interface. The TCP interface simply
streams the P1 data read via the serial port: each telegram read on the input
is replicated on the output. This replication of the stream provides other
(monitoring) tools access to the same P1 data. The UDP interface provides a
simple request/response interface. Type 'help' as command to get an overview
of the available commands.
