# Build the PA6H receiver driver when enabled in Kconfig.
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include)

if(CONFIG_PA6H)
  zephyr_library()
  zephyr_library_sources(pa6h.c)
endif()
