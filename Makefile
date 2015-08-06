.PHONY: all

TARGETS=la16fw-fpga-18.bitstream la16fw-fpga-33.bitstream la16fw-fx2.fw

SOURCE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

all: $(addprefix bin/,$(TARGETS))

bin/%.bitstream:
	sed -i -re 's/(NET "logic_data\[[0-9]+\]" IOSTANDARD = )[^;]*/\1LVCMOS$(subst la16fw-fpga-,,$(basename $(notdir $@)))/g' main.ucf
	$(MAKE) mainmodule.bit
	mv mainmodule.bit $@

bin/la16fw-fx2.fw:
	$(MAKE) -C fx2
	cp fx2/build/logic16.bix bin/la16fw-fx2.fw

mainmodule.bit: temp
	xst -intstyle ise -ifn mainmodule.xst -ofn mainmodule.syr
	ngdbuild -intstyle ise -dd _ngo -nt timestamp -uc main.ucf -p xc3s200a-vq100-4 mainmodule.ngc mainmodule.ngd
	map -intstyle ise -p xc3s200a-vq100-4 -cm area -ir off -pr off -c 100 -o mainmodule_map.ncd mainmodule.ngd mainmodule.pcf
	par -w -intstyle ise -ol high -t 1 mainmodule_map.ncd mainmodule.ncd mainmodule.pcf
	trce -intstyle ise -v 3 -s 4 -n 3 -fastpaths -xml mainmodule.twx mainmodule.ncd -o mainmodule.twr mainmodule.pcf -ucf main.ucf
	bitgen -intstyle ise -f mainmodule.ut mainmodule.ncd

temp:
	mkdir temp
