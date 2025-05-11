AM_CFLAGS = -g -Wall -D_GNU_SOURCE

AUTOMAKE_OPTIONS = foreign subdir-objects

hgrnic_version_script = @HGRNIC_VERSION_SCRIPT@

HGRNIC_SOURCES = src/ah.c src/buf.c src/cq.c src/hgrnic.c \
    src/qp.c src/verbs.c src/srq.c

if HAVE_IBV_DEVICE_LIBRARY_EXTENSION
    lib_LTLIBRARIES = src/libhgrnic.la
    src_libhgrnic_la_SOURCES = $(HGRNIC_SOURCES)
    src_libhgrnic_la_LDFLAGS = -avoid-version -release @IBV_DEVICE_LIBRARY_EXTENSION@ \
        $(hgrnic_version_script)
    hgrnicconfdir = $(sysconfdir)/libibverbs.d
    hgrnicconf_DATA = hgrnic.driver
else
    hgrniclibdir = $(libdir)/infiniband
    hgrniclib_LTLIBRARIES = src/hgrnic.la
    src_hgrnic_la_SOURCES = $(HGRNIC_SOURCES)
    src_hgrnic_la_LDFLAGS = -avoid-version -module $(hgrnic_version_script)
endif

DEBIAN = debian/changelog debian/compat debian/control debian/copyright \
    debian/libhgrnic1.install debian/libhgrnic-dev.install debian/rules

EXTRA_DIST = src/doorbell.h src/hgrnic.h src/hgrnic-abi.h src/wqe.h \
    hgrnic.map hgrnic.driver
