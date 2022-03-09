
## DTB overlay patch module

Many systems use ACPI instead of DTB. Since there's no DTB tree, you
won't be able to use the DTB overlay to patch and update some device
specs when system is alive. This kernel module creates an empty root
DTB node, so that you could apply whatever DTBO over it.

## Build DTBO from DTS

`./dtc/dtc -@ -O dtb -o test.dtbo test.dtsi`

## Install DTBO

`sudo insmod dtboverlay/dtboverlay_out.ko dtbpath=/home/hu/ETM/test.dtbo`

## Uninstall DTBO

`TODO: the code is very hackish. There's no guarantee for safe uninstall ATM.`
