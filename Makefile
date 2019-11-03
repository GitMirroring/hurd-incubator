PKGDIR        ?= /usr/share/libdde_linux26
L4DIR         ?= $(PKGDIR)

include Makeconf.local

TARGET         = netdde

SRC_C          = main.c check_kernel.c

LIBS          += $(libmachdev_path) -ldde_linux26.o -ldde_linux26_net $(libddekit_path) -lfshelp -ltrivfs -lpciaccess -lz -lpthread -lshouldbeinlibc -lports $(libslab_path) $(libbpf_path)
CFLAGS        += -g -I$(PKGDIR)/include -I$(BUILDDIR)/include
LDFLAGS       += -g

CFLAGS += -DCONFIG_B44_PCI -DCONFIG_8139TOO_8129

# DDE configuration
include $(L4DIR)/Makeconf

include $(L4DIR)/mk/prog.mk

SRC=linux/drivers/net

# TODO: should take driver order from Linux
BLACKLIST = \
	    This is a dumb driver \
	    $(SRC)/pci-skeleton.c \
	    \
	    These dont build at all \
	    \
	    $(SRC)/netx-eth.c $(SRC)/smc911x.c $(wildcard $(SRC)/irda/*) \
	    $(wildcard $(SRC)/arm/*.c) $(SRC)/bfin_mac.c $(SRC)/s6gmac.c \
	    $(SRC)/declance.c $(wildcard $(SRC)/ehea/*) $(wildcard $(SRC)/*82596.c) \
	    $(SRC)/atlx/atl1.c $(SRC)/atlx/atlx.c $(SRC)/xilinx_emaclite.c $(wildcard $(SRC)/ll_temac_*.c) \
	    $(SRC)/davinci_emac.c $(SRC)/pasemi_mac_ethtool.c \
	    $(wildcard $(SRC)/wireless/*) $(wildcard $(SRC)/wireless/*/*) $(wildcard $(SRC)/wireless/*/*/*) \
	    $(SRC)/apne.c \
	    $(SRC)/sunbmac.c $(SRC)/sunqe.c $(SRC)/myri_sbus.c $(SRC)/sunlance.c $(SRC)/sunhme.c \
	    $(SRC)/sungem_phy.c $(SRC)/sungem.c $(SRC)/mace.c $(SRC)/bmac.c $(SRC)/sunvnet.c \
	    $(SRC)/gianfar_mii.c $(SRC)/gianfar.c $(SRC)/fec_mpc52xx_phy.c $(SRC)/ucc_geth_mii.c \
	    $(wildcard $(SRC)/fs_e$(SRC)/*) \
	    $(wildcard $(SRC)/sun3*) \
	    $(SRC)/au1000_eth.c \
	    $(SRC)/s2io.c $(SRC)/sonic.c \
	    $(SRC)/cnic.c \
	    $(wildcard $(SRC)/wimax/i2400m/*) \
	    $(wildcard $(SRC)/ibm_newemac/*) \
	    $(wildcard $(SRC)/stmmac/*) \
	    $(wildcard $(SRC)/hamradio/*) \
	    $(wildcard $(SRC)/usb/*) \
	    $(wildcard $(SRC)/sfc/*) \
	    $(wildcard $(SRC)/appletalk/*) \
	    $(wildcard $(SRC)/pcmcia/*) \
	    $(wildcard $(SRC)/tokenring/*) \
	    $(wildcard $(SRC)/wan/*) \
	    $(wildcard $(SRC)/wan/*/*) \
	    $(wildcard $(SRC)/can/*) \
	    $(wildcard $(SRC)/can/*/*) \
	    $(wildcard $(SRC)/cris/*) \
	    $(wildcard $(SRC)/fs_enet/*) \
	    $(wildcard $(SRC)/arcnet/*) \
	    $(wildcard $(SRC)/benet/*) \
	    $(wildcard $(SRC)/arc$(SRC)/*) \
	    $(SRC)/mv643xx_eth.c $(SRC)/pasemi_mac.c $(SRC)/ibmveth.c \
	    $(wildcard $(SRC)/ps3*) \
	    $(SRC)/iseries_veth.c $(SRC)/cpmac.c $(SRC)/ixgbe/ixgbe_dcb_nl.c \
	    $(wildcard $(SRC)/ucc_geth*) \
	    $(wildcard $(SRC)/mlx4/*) \
	    $(wildcard $(SRC)/bonding/*) \
	    $(SRC)/ariadne.c $(SRC)/a2065.c $(SRC)/zorro8390.c $(SRC)/hydra.c \
	    $(SRC)/macmace.c $(SRC)/dm9000.c $(SRC)/ax88796.c $(SRC)/rionet.c $(SRC)/sgiseeq.c \
	    $(SRC)/ioc3-eth.c $(SRC)/fec_mpc52xx.c $(SRC)/hplance.c $(SRC)/atarilance.c \
	    $(SRC)/macsonic.c $(SRC)/xen-netfront.c $(SRC)/jazzsonic.c $(SRC)/mac89x0.c \
	    $(wildcard $(SRC)/ixp2000/*) \
	    $(wildcard $(SRC)/skfp/*) \
	    $(SRC)/mac8390.c $(SRC)/isa-skeleton.c $(SRC)/sh_eth.c $(SRC)/spider_net.c \
	    $(SRC)/phy/fixed.c $(SRC)/stnic.c $(SRC)/mvme147.c $(SRC)/meth.c $(SRC)/macb.c \
	    $(SRC)/korina.c $(SRC)/b44.c $(SRC)/sb1250-mac.c $(SRC)/fec.c $(SRC)/ne-h8300.c \
	    $(SRC)/mipsnet.c $(SRC)/tsi108_eth.c \
	    $(SRC)/fsl_pq_mdio.c \
	    $(SRC)/bcm63xx_enet.c \
	    \
	    $(wildcard $(SRC)/cxgb3/*) \
	    $(SRC)/ifb.c \
	    $(wildcard $(SRC)/igb/*) \
	    $(wildcard $(SRC)/ixgbe/*) \
	    $(SRC)/macvlan.c $(SRC)/niu.c \
	    $(SRC)/smsc911x.c \
	    $(SRC)/tulip/dmfe.c \
	    $(wildcard $(SRC)/vxge/*) \
	    $(SRC)/xtsonic.c \
	    \
	    These are not usedful \
	    \
	    $(SRC)/loopback.c \
	    $(SRC)/eql.c \
	    $(SRC)/ppp_generic.c $(SRC)/ppp_mppe.c $(SRC)/plip.c \
	    $(SRC)/ppp_synctty.c $(SRC)/ppp_async.c $(SRC)/bsd_comp.c \
	    $(SRC)/pppox.c $(SRC)/pppoe.c $(SRC)/pppol2tp.c $(SRC)/ppp_deflate.c \
	    $(SRC)/tun.c \
	    $(SRC)/slip.c $(SRC)/netconsole.c \
	    $(SRC)/veth.c $(SRC)/virtio_net.c \
	    $(SRC)/atp.c \
	    \
	    These are missing some symbols \
	    \
	    missing zlib_* crc32c \
	    $(wildcard $(SRC)/bnx2x_*.c) \
	    missing ktime_get_ts \
	    $(SRC)/chelsio/sge.c \
	    $(SRC)/chelsio/subr.c \
	    $(SRC)/chelsio/cxgb2.c \
	    missing alloc_fddidev fddi_type_trans \
	    $(SRC)/defxx.c \
	    missing spi_sync spi_write_then_read spi_register_driver \
	    $(SRC)/enc28j60.c \
	    missing lro_* \
	    $(wildcard $(SRC)/enic/*) \
	    missing mca_* \
	    $(SRC)/ibmlana.c \
	    missing spi_sync spi_register_driver  \
	    $(SRC)/ks8851.c \
	    $(SRC)/ks8842.c \
	    missing __iowrite64_copy lro_* mtrr_add ioremap_wc mtrr_del \
	    $(wildcard $(SRC)/myri10ge/*) \
	    missing high_memory \
	    $(SRC)/ne3210.c \
	    missing *hippi* \
	    $(SRC)/rrunner.c \
	    missing readsl/w writesl/w \
	    $(SRC)/smc91x.c \

# This one is included by others, don't build it by itself!
NO_BUILD = dde/lib8390.c

SRC_ORIG := $(filter-out $(BLACKLIST),$(shell find $(SRC) -name \*.c | LC_ALL=C sort))
SRC_TARGET := $(patsubst $(SRC)/%,dde/%,$(SRC_ORIG))

SRC_C		+= $(filter-out $(NO_BUILD),$(SRC_TARGET))

.PHONY: convert
convert:
	mkdir -p dde
	for i in $(SRC_ORIG) ; do \
		TARGET="dde/$${i#$(SRC)/}" ; \
		mkdir -p "$$(dirname "$$TARGET")" ; \
		./convert "$$i" > "$$TARGET"; \
	done
	for i in $$(find $(SRC) -name \*.h) ; do \
		TARGET="dde/$${i#$(SRC)/}" ; \
		mkdir -p "$$(dirname "$$TARGET")" ; \
		ln -f "$$i" "$$TARGET" ; \
	done
	cd dde ; patch -p1 < ../patch

klean: clean
	rm -fr dde
