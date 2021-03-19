all:

.PHONY: clean
clean:
	find fs -name "*.o" | xargs rm -f
	find fs -name "Kconfig*" | xargs rm -f
	find fs -name "modules*" | xargs rm -f
	find fs -name "*.ko" | xargs rm -f
	find fs -name "*.mod.c" | xargs rm -f
