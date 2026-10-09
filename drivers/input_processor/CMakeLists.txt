zephyr_library_named(zmk_config_input_processor)

# drivers/input_processor.h lives in ZMK's application include directory,
# which app/CMakeLists.txt adds PRIVATE to its own target, so an out-of-tree
# processor does not inherit it. The Zephyr application is zmk/app in any ZMK
# build, so resolve it from there and fail loudly if that stops being true.
set(zmk_app_include ${APPLICATION_SOURCE_DIR}/include)

if(NOT EXISTS ${zmk_app_include}/drivers/input_processor.h)
  message(FATAL_ERROR
    "ZMK's input_processor.h was not found under ${zmk_app_include}. "
    "The acceleration input processor needs it; check that the application "
    "really is zmk/app.")
endif()

zephyr_library_include_directories(${zmk_app_include})

zephyr_library_sources(input_processor_acceleration.c)
