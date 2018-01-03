Part 2 - Dynamic Instrumentation of Kernel module
============================================================================================
It contains 4 files:
user_rbt530.c
rbt530.h
kprobe530.h
libioctl.h
rbt530_drv.c
kprobe_drv.c
Makefile

user_rbt530.c	->	This is a user space app to test kernel driver rbt530_drv to access a kernel RB Tree device. It defines the operations (read, write, setend, dumptree) to perform on the device tree.
The app spawns 4 threads and initiates the RB-tree with 40 objects and displays a completion message.
After writing 40 objects the multithreaded app performs random R/W file operations on the device rb-tree. After all the threads exit, the main thread dumps out the remaining nodes of the tree.

rbt530_drv.c	->	This is a device driver implemented to perform file operations on the rbt530_dev device to augment the tree nodes . It implements the ioctl commands and read/write
operations.
Read operation 	- Displays the First or Last object node of the tree based on the flag set by the user app.
Write operation - Insert a node into the tree if it has a unique key and data pair, and data is not zero.
SETEND 			- This command is used to set the reader flag to 0 or 1.
DUMPTREE 		- This command is used to print all the existing nodes of the tree.
PRINT 			- This is used to print the completion message of 40 objects written to tree.

kprobe_drv.c		->	Implements kprobe driver to insert a probe at a user specified offset. This inserts a probe in function rb_insert() at an offset 0xff. Store the elements traversed in an local array and returns it to the user on a kprobe read.
Write()
If the user sets the flag to 1, then the kprobe write function register the probe at a location(offset).
If the user sets the flag to 0, then kprobe is unregistered.
Read()
Retrieve the trae data items collected ina probe hit and saved in the buffer.

kprobe530.h		-> 	This contains kprobe buffer used to store the pid, addr, timestamp and elements traveresed when the probe is hit.

rbt530.h 		->	This contains rb_object structure containing the key,data members for use by the kernel device and user app

libioctl.h		->	This contains the ioctl commands definition.

Makefile		->	To make the target object file of the i2cflash driver.


----------------------------------------------------------------------------------------------------------------------------------
Compilation & Usage Steps:
=========================================================================================
Inorder to compile the driver kernel object file:
----------------------------------------------------------------------------------------------------------------------------------

NOTE:
1) Remove any prior kprobe_dev & rbt530_dev dev node from the the Galileo Gen2 board.
rmmod rbt530_drv.ko
rmmod kprobe_drv.ko

2) Export your SDK's toolchain path to your current directory to avoid unrecognized command.
export PATH="/opt/iot-devkit/1.7.2/sysroots/x86_64-pokysdk-linux/usr/bin/i586-poky-linux/:$PATH"
=================================================================================================

Method:-

1) Change the following KDIR path to your source kernel path and the SROOT path to your sysroots path in the Makefile
KDIR:=/opt/iot-devkit/1.7.2/sysroots/i586-poky-linux/usr/src/kernel
SROOT=/opt/iot-devkit/1.7.2/sysroots/i586-poky-linux/

2) Run make command to compile both user app and the rbt530_dev, kprobe_dev kernel objects.
make

3) copy the app exe and driver object files from current dir to board dir:
sudo scp <filename> root@<inet_address>:/home/root
Files
rbt530_drv.ko
kprobe_drv.ko
app

4) On the Galileo Gen2 board, insert the kernel module:
insmod rbt530_drv.ko
insmod kprobe_drv.ko

5) Run the executable "app" 
./app > user.txt


=======================================================================================================================================================================================
