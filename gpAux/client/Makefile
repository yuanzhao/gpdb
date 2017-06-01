all: clients
top_builddir:=../..
INSTLOC:=$(top_builddir)

CLIENT_HOME_DIR:=$(top_builddir)
LOADERS_HOME_DIR:=$(top_builddir)

CLIENTINSTLOC=$(CLIENT_HOME_DIR)/greenplum-clients
CLIENTINSTLOC_BIN=$(CLIENTINSTLOC)/bin
CLIENTINSTLOC_LIB=$(CLIENTINSTLOC)/lib
CLIENTINSTLOC_INCLUDE=$(CLIENTINSTLOC)/include
CLIENTINSTLOC_DOCS=$(CLIENTINSTLOC)/docs
CLIENTINSTLOC_OPENSOURCE=$(CLIENTINSTLOC)/thirdparty

LOADERSINSTLOC=$(LOADERS_HOME_DIR)/greenplum-loaders
LOADERSINSTLOC_BIN=$(LOADERSINSTLOC)/bin
LOADERSINSTLOC_BINEXT=$(LOADERSINSTLOC)/bin/ext/
LOADERSINSTLOC_EXT=$(LOADERSINSTLOC)/ext
LOADERSINSTLOC_LIB=$(LOADERSINSTLOC)/lib
LOADERSINSTLOC_LIB_PWARE=$(LOADERSINSTLOC)/lib/pware
LOADERSINSTLOC_DOCS=$(LOADERSINSTLOC)/docs
LOADERSINSTLOC_OPENSOURCE=$(LOADERSINSTLOC)/thirdparty
include $(top_builddir)/src/Makefile.global

define tmpCLIENT_FILESET
	pg_dump
	pg_dumpall
	psql
endef
CLIENT_FILESET = $(strip $(tmpCLIENT_FILESET))
install: clients
	echo "installing clients"

clients: 
	if [ "$(enable_clients)" = "yes" ]; then $(MAKE) clients_internal; fi
clients_internal:
	mkdir -p $(CLIENTINSTLOC_BIN)
	(cd $(INSTLOC)/bin && $(TAR) cf - $(CLIENT_FILESET)) | (cd $(CLIENTINSTLOC_BIN)/ && $(TAR) xpf -)$(check_pipe_for_errors)
	mkdir -p $(CLIENTINSTLOC_INCLUDE)
	(cd $(INSTLOC)/include && $(TAR) cf - *) | (cd $(CLIENTINSTLOC_INCLUDE)/ && $(TAR) xpf -)$(check_pipe_for_errors)
	mkdir -p $(CLIENTINSTLOC_LIB)
	(cd $(INSTLOC)/lib && $(TAR) cf - *) | (cd $(CLIENTINSTLOC_LIB)/ && $(TAR) xpf -)$(check_pipe_for_errors)
ifneq "$(FILESET_LIB_GCC)" ""
	(cd $(CLIENTINSTLOC_LIB)/ && $(TAR) cf - -C $(FILESET_LIB_GCC_LOC) $(FILESET_LIB_GCC) | $(TAR) xpf -)$(check_pipe_for_errors)
endif
	# ---- copy license files ----
	cp ${BLD_TOP}/../LICENSE $(CLIENTINSTLOC)
	cp ${BLD_TOP}/../NOTICE $(CLIENTINSTLOC)

	# ---- create the installer ----
	$(warn MPP_ARCH=$(MPP_ARCH))

	# ---- Updating the version in required packages ----
	@$(MAKE) set_scripts_version INSTLOC=$(CLIENTINSTLOC)
	cd install/src/unix/ && \
	$(MAKE) TAR=$(TAR) PRODUCT=Clients PACKAGE=greenplum-clients README=GPClientToolsUnix VERSION=`perl -pe 's, ,-,g' ../../../../../VERSION` INSTALL_SRC=$(CLIENTINSTLOC) INSTALL_DIR=../../../..
