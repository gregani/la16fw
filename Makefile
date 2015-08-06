.PHONY: all fpga fx2 clean

TARGETS_FPGA=la16fw-fpga-18.bitstream la16fw-fpga-33.bitstream
TARGETS_FX2=la16fw-fx2.fw
TARGETS=$(TARGETS_FPGA) $(TARGETS_FX2)

SOURCE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

all: fpga fx2
fpga: $(addprefix bin/,$(TARGETS_FPGA))
fx2: $(addprefix bin/,$(TARGETS_FX2))

bin/la16fw-fx2.fw: fx2/build/logic16.bix
	cp fx2/build/logic16.bix bin/la16fw-fx2.fw

fx2/build/logic16.bix:
	$(MAKE) -C fx2

bin/%.bitstream:
	sed -i -re 's/(NET "logic_data\[[0-9]+\]" IOSTANDARD = )[^;]*/\1LVCMOS$(subst la16fw-fpga-,,$(basename $(notdir $@)))/g' main.ucf
	$(MAKE) mainmodule.bit
	mv mainmodule.bit $@

mainmodule.bit: temp
	xst -intstyle ise -ifn mainmodule.xst -ofn mainmodule.syr
	ngdbuild -intstyle ise -dd _ngo -nt timestamp -uc main.ucf -p xc3s200a-vq100-4 mainmodule.ngc mainmodule.ngd
	map -intstyle ise -p xc3s200a-vq100-4 -cm area -ir off -pr off -c 100 -o mainmodule_map.ncd mainmodule.ngd mainmodule.pcf
	par -w -intstyle ise -ol high -t 1 mainmodule_map.ncd mainmodule.ncd mainmodule.pcf
	trce -intstyle ise -v 3 -s 4 -n 3 -fastpaths -xml mainmodule.twx mainmodule.ncd -o mainmodule.twr mainmodule.pcf -ucf main.ucf
	bitgen -intstyle ise -f mainmodule.ut mainmodule.ncd

temp:
	mkdir temp

clean:
	-rm $(addprefix bin/,$(TARGETS))
	-rm $(addprefix mainmodule.,bgn bld drc lso ncd ngc ngd ngr pad par pcf ptwx syr twr twx unroutes xpi)
	-rm $(addprefix mainmodule_,bitgen.xwbt guide.ncd map.map map.mrp map.ncd map.ngm map.xrpt ngdbuild.xrpt pad.csv pad.txt par.xrpt summary.html summary.xml usage.xml xst.xrpt)
	-rm usage_statistics_webtalk.html webtalk.log _ngo/netlist.lst
	-rm $(addsuffix .xmsgs,$(addprefix _xmsgs/,bitgen map ngdbuild par pn_parser trce xst))
	-rm xlnx_auto_0_xdb/cst.xbcd
	-rmdir _ngo _xmsgs xlnx_auto_0_xdb
	-rm $(addsuffix .vho,$(addprefix xst/work/sub00/vhpl,00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19))
	-rm $(addsuffix .ref,$(addprefix xst/work/,hdllib hdpdeps))
	-rmdir -p xst/dump.xst/mainmodule.prj/ngx/notopt
	-rmdir -p xst/dump.xst/mainmodule.prj/ngx/opt
	-rmdir -p xst/file\ graph
	-rmdir -p xst/work/sub00
	$(MAKE) -C fx2 clean
