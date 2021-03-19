KERNEVFS=../include/uapi/linux/evfs.h

all: kernevfs.h

kernevfs.h: $(KERNEVFS)
	ln -s $(KERNEVFS) $@
	
.PHONY: clean
clean:
	rm -rf kernevfs.h
