obj-m += wake_latency.o

all: wake_latency.ko wl_test

cflags = -Wall -lpthread -lm -g

wake_latency.ko: wake_latency.c	 wake_latency.h wake_latency_device.h
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

wl_test: wl_test.c wake_latency_device.h
	gcc -o wl_test wl_test.c $(cflags)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -rf Module.markers
	rm -rf modules.order
	rm -rf wl_test
