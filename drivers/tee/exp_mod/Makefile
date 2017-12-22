obj-m := mod.o #../../../fs/namei.o

#obj-m += ofs.o
#ofs-objs += mod.o /home/guo405/optee_dev/linux/fs/namei.o


all:
	    make -C /home/guo405/optee_dev/linux M=$(PWD) modules

clean:
	    make -C /home/guo405/optee_dev/linux M=$(PWD) clean
