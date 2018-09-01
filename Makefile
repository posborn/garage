PROJECT_NAME = garage

CFLAGS += -I$(abspath ../esp-homekit-demo) -DHOMEKIT_SHORT_APPLE_UUIDS

EXTRA_COMPONENT_DIRS += $(abspath ../esp-homekit-demo/components)

include $(IDF_PATH)/make/project.mk
