LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := debug
LOCAL_STATIC_LIBRARIES := libnl \
			  libpci_static \

#			  libtraceevent \

LOCAL_SHARED_LIBRARIES := libstlport \
			  libncurses

LOCAL_MODULE := powertop

LOCAL_CPPFLAGS += -DDISABLE_I18N -DPACKAGE_VERSION=\"2.5\" -DPACKAGE=powertop -DDISABLE_TRYCATCH -DHAVE_LIBNL20 -Wno-error=date-time -static

LOCAL_CFLAGS += -DHAVE_LIBNL20 -static

#LOCAL_CFLAGS += -Wall -O2 -g -fno-omit-frame-pointer -fstack-protector -Wshadow -Wformat -D_FORTIFY_SOURCE=2
#LOCAL_CPPFLAGS += -Wall -O2 -g -fno-omit-frame-pointer

LOCAL_C_INCLUDES += external/libnl/include/ external/libncurses/include external/pciutils/include/ external/libnl-headers/
#include external/stlport/libstlport.mk

LOCAL_SRC_FILES += \
	src/parameters/parameters.cpp \
	src/parameters/persistent.cpp \
	src/parameters/learn.cpp \
	src/process/powerconsumer.cpp \
	src/process/work.cpp \
	src/process/process.cpp \
	src/process/timer.cpp \
	src/process/processdevice.cpp \
	src/process/interrupt.cpp \
	src/process/do_process.cpp \
	src/cpu/intel_cpus.cpp \
	src/cpu/intel_gpu.cpp \
	src/cpu/cpu.cpp \
	src/cpu/cpu_linux.cpp \
	src/cpu/cpudevice.cpp \
	src/cpu/cpu_core.cpp \
	src/cpu/cpu_package.cpp \
	src/cpu/abstract_cpu.cpp \
	src/cpu/cpu_rapl_device.cpp \
	src/cpu/dram_rapl_device.cpp \
	src/cpu/rapl/rapl_interface.cpp \
	src/measurement/measurement.cpp \
	src/measurement/acpi.cpp \
	src/measurement/extech.cpp \
	src/measurement/sysfs.cpp \
	src/display.cpp \
	src/report/report.cpp \
	src/report/report-maker.cpp \
	src/report/report-formatter-base.cpp \
	src/report/report-formatter-csv.cpp \
	src/report/report-formatter-html.cpp \
	src/main.cpp \
	src/tuning/tuning.cpp \
	src/tuning/tuningusb.cpp \
	src/tuning/bluetooth.cpp \
	src/tuning/ethernet.cpp \
	src/tuning/runtime.cpp \
	src/tuning/iw.c \
	src/tuning/tunable.cpp \
	src/tuning/tuningsysfs.cpp \
	src/tuning/cpufreq.cpp \
	src/tuning/wifi.cpp \
	src/perf/perf_bundle.cpp \
	src/perf/perf.cpp \
	src/devices/thinkpad-fan.cpp \
	src/devices/alsa.cpp \
	src/devices/runtime_pm.cpp \
	src/devices/usb.cpp \
	src/devices/ahci.cpp \
	src/devices/rfkill.cpp \
	src/devices/thinkpad-light.cpp \
	src/devices/i915-gpu.cpp \
	src/devices/backlight.cpp \
	src/devices/network.cpp \
	src/devices/device.cpp \
	src/devices/gpu_rapl_device.cpp \
	src/devlist.cpp \
	src/calibrate/calibrate.cpp \
	src/lib.cpp \
	traceevent/event-parse.c \
	traceevent/parse-filter.c \
	traceevent/parse-utils.c \
	traceevent/trace-seq.c

include $(BUILD_EXECUTABLE)
