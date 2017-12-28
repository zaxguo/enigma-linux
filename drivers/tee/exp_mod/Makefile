obj-m := ofs.o 
ofs-objs := mod.o ofs_fs_handler.o 

#obj-m += ofs.o
#ofs-objs += mod.o /home/guo405/optee_dev/linux/fs/namei.o


all:
	    make -C /home/guo405/optee_dev/linux M=$(PWD) modules

clean:
	    make -C /home/guo405/optee_dev/linux M=$(PWD) clean
