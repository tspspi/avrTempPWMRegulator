CPUFREQ=16000000L
FLASHDEV=/dev/ttyU0
I2CADR=0x14

all: bin/avr_tempregulator.hex

bin/avr_tempregulator.bin: src/avr_tempregulator.c ./src/sysclk.c ./src/pwmout.c ./src/serial.c

	avr-gcc -Wall -Os -mmcu=atmega328p -DF_CPU=$(CPUFREQ) -DSTEPPER_I2C_ADDRESS=$(I2CADR) -o bin/avr_tempregulator.bin src/avr_tempregulator.c  src/sysclk.c src/pwmout.c src/serial.c

bin/avr_tempregulator.hex: bin/avr_tempregulator.bin

	avr-size -t bin/avr_tempregulator.bin
	avr-objcopy -j .text -j .data -O ihex bin/avr_tempregulator.bin bin/avr_tempregulator.hex

flash: bin/avr_tempregulator.hex

#	sudo chmod 666 $(FLASHDEV)
#	avrdude -v -p atmega328p -c avrisp -P /dev/ttyU0 -b 57600 -D -U flash:w:bin/avr_tempregulator.hex:i
	avrdude -v -p atmega328p -c arduino -P COM9 -b 57600 -e -U flash:w:bin/avr_tempregulator.hex:i

framac: src/avr_tempregulator.c src/sysclk.c src/pwmout.c src/serial.c

	-rm framacreport.csv
	frama-c -wp-verbose 0 -wp -rte -wp-rte -wp-dynamic -wp-timeout 300 -cpp-extra-args="-I/usr/home/tsp/framaclib/ -DF_CPU=16000000L -D__AVR_ATmega328P__ -DFRAMAC_SKIP" src/avr_tempregulator.c src/sysclk.c src/pwmout.c src/serial.c -then -no-unicode -report -report-csv framacreport.csv

clean:

	-rm bin/*.bin

cleanall: clean

	-rm bin/*.hex

.PHONY: all clean cleanall
