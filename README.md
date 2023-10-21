# bme680fs
A Plan9 (9p) file system for sgp30 environmental sensor on the I2C bus.

## about

sgp30fs provides control for a [sgp30](https://sensirion.com/products/catalog/SGP30/) environmental sensor, connected to a [I2C](https://en.wikipedia.org/wiki/I%C2%B2C) bus.

It provides a file in /srv.

`/srv/sgp30fs`

And automatically moutn in the current namespace in /mnt, providing a ctl file for control and files for accessing environmental measurements.

`/mnt/bme680/ctl`

`/mnt/bme680/co2e`

`/mnt/bme680/tvoc`

`/mnt/bme680/all`


The  module used for development and testing was the seed studio [Grove-VOC and eCO2 Gas Sensor(SGP30)](https://wiki.seeedstudio.com/Grove-VOC_and_eCO2_Gas_Sensor-SGP30/) connected to i2c1 on a Raspberry Pi 1 running 9front with I2C enabled.

## requirements

A computer with I2C IO (eg. Raspberry Pi) running Plan9 or 9front with I2C support enabled/added.

## building

`mk`

## usage

Running bme680fs will start the program and provide a file in /srv. In the namespace where it was run, it will also mount ctl and display interface files in /mnt.

### starting

The sgp30 can be started by calling the sgp30 binary.

It provides flags to control the behavior:

- -d : run continuous sampling in a thread that affects the gas measurements (run in the background when starting)

Please reference the startsgp30.rc script.

### mounting

Once it has been run, it can be accessed from other namespaces by mounting the srv file

`mount -b /srv/sgp30fs /mnt`

### controling

The program provides __sgp30/ctl__ control file for controlling the sensor:

Cating the file will print out the current parameters for running measurements, as described in the datasheet.

`cat /mnt/sgp30/ctl`

No detailed explanation here, if you don't know what they are, don't mess with them.

### reading values

Cating individual files will execute a measurement with those environmental sensors and display the result

## notes

Yes, it can always be better.
