CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror \
	-I../../firmware/exp066_research_platform_core/src
BUILD := build
CORE_TARGET := $(BUILD)/test_rsm_core
VECTOR_TARGET := $(BUILD)/test_rsm_vector
LOG_TARGET := $(BUILD)/test_rsm_log
RUNTIME_DEFAULT_TARGET := $(BUILD)/test_runtime_monitor_default
RUNTIME_DISABLED_TARGET := $(BUILD)/test_runtime_monitor_disabled
RUNTIME_VECTOR_DISABLED_TARGET := $(BUILD)/test_runtime_monitor_vector_disabled
RUNTIME_RESTRICTED_TARGET := $(BUILD)/test_runtime_monitor_restricted
CORE_SOURCES := test_rsm_core.c \
	../../firmware/exp066_research_platform_core/src/runtime_monitor_core.c
VECTOR_SOURCES := test_rsm_vector.c \
	../../firmware/exp066_research_platform_core/src/runtime_monitor_vector.c \
	../../firmware/exp066_research_platform_core/src/runtime_monitor_core.c
LOG_SOURCES := test_rsm_log.c \
	../../firmware/exp066_research_platform_core/src/log.c
RUNTIME_SOURCES := test_runtime_monitor_host.c \
	../../firmware/exp066_research_platform_core/src/runtime_monitor.c \
	../../firmware/exp066_research_platform_core/src/runtime_monitor_core.c \
	../../firmware/exp066_research_platform_core/src/runtime_monitor_vector.c

.PHONY: test clean

test: $(CORE_TARGET) $(VECTOR_TARGET) $(LOG_TARGET) $(RUNTIME_DEFAULT_TARGET) \
		$(RUNTIME_DISABLED_TARGET) $(RUNTIME_VECTOR_DISABLED_TARGET) \
		$(RUNTIME_RESTRICTED_TARGET)
	$(CORE_TARGET)
	$(VECTOR_TARGET)
	$(LOG_TARGET)
	$(RUNTIME_DEFAULT_TARGET)
	$(RUNTIME_DISABLED_TARGET)
	$(RUNTIME_VECTOR_DISABLED_TARGET)
	$(RUNTIME_RESTRICTED_TARGET)

$(BUILD):
	mkdir -p $@

$(CORE_TARGET): $(CORE_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) $(CORE_SOURCES) -o $@

$(VECTOR_TARGET): $(VECTOR_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) $(VECTOR_SOURCES) -o $@

$(LOG_TARGET): $(LOG_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -DLOG_HOST_TEST -I../../firmware/common \
		$(LOG_SOURCES) -o $@

$(RUNTIME_DEFAULT_TARGET): $(RUNTIME_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -DRSM_HOST_TEST -I../../firmware/common \
		-I../../firmware/exp045_bootloader_v2/src \
		-I../../firmware/exp045_bootloader_v2/include \
		$(RUNTIME_SOURCES) -o $@

$(RUNTIME_DISABLED_TARGET): $(RUNTIME_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -DRSM_HOST_TEST -DRSM_ENABLE=0 -I../../firmware/common \
		-I../../firmware/exp045_bootloader_v2/src \
		-I../../firmware/exp045_bootloader_v2/include \
		$(RUNTIME_SOURCES) -o $@

$(RUNTIME_VECTOR_DISABLED_TARGET): $(RUNTIME_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -DRSM_HOST_TEST -DRSM_VECTOR_MONITOR_ENABLE=0 \
		-I../../firmware/common \
		-I../../firmware/exp045_bootloader_v2/src \
		-I../../firmware/exp045_bootloader_v2/include \
		$(RUNTIME_SOURCES) -o $@

$(RUNTIME_RESTRICTED_TARGET): $(RUNTIME_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) -DRSM_HOST_TEST -DRSM_RESTRICTED_DIAGNOSTICS=1 \
		-I../../firmware/common \
		-I../../firmware/exp045_bootloader_v2/src \
		-I../../firmware/exp045_bootloader_v2/include \
		$(RUNTIME_SOURCES) -o $@

clean:
	rm -rf $(BUILD)
