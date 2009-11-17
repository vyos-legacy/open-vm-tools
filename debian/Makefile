OVT_SOURCE_DIR=/usr/src/modules/open-vm

all:	build

build:
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/pvscsi
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmblock
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmci
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmhgfs
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmmemctl
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmsync
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmxnet
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmxnet3
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vsock

clean:
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/pvscsi clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmblock clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmci clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmhgfs clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmmemctl clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmsync clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmxnet clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vmxnet3 clean
	$(MAKE) OVT_SOURCE_DIR=$(OVT_SOURCE_DIR) -C modules/linux/vsock clean

distclean:	clean
