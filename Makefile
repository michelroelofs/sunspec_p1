c_files:=main.c reporter.c interface.c
h_files:=interface.h

all: measurement
clean:
	@rm measurement
#all: measurement dumpdata power_rrdtool_update

measurement: $(c_files) $(h_files)
	$(CC) -g -o $@ $^

dumpdata: dumpdata.c interface.h
	$(CC) -lpthread -g -o $@ $^

power_rrdtool_update: power_rrdtool_update.c
	$(CC) -o $@ $^

updateValue: updateValue.c
	$(CC) -o $@ $^
