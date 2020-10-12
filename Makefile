obj-m += TimedMessagingSystem.o
TimedMessagingSystem-objs := sys.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
