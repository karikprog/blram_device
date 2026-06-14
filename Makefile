KVER ?= $(shell uname -r)

ifneq (,$(wildcard /usr/src/kernels/$(KVER)))
LK_BUILD_DIR ?= /usr/src/kernels/$(KVER)
else
LK_BUILD_DIR ?= /lib/modules/$(KVER)/build
endif

all: build
build:
	$(MAKE) -j -C $(LK_BUILD_DIR) M=$(PWD) modules
clean:
	$(MAKE) -j -C $(LK_BUILD_DIR) M=$(PWD) clean