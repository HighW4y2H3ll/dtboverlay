obj-m += dtboverlay_out.o ofcheck.o
dtboverlay_out-objs := dtboverlay.o libfdt/fdt.o

all:
	make -C /home/hu/linux/build M=$(PWD) modules

clean:
	make -C /home/hu/linux/build M=$(PWD) clean

# vim: set expandtab!:
