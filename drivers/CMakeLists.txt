# Guarded on the drivers themselves rather than CONFIG_INPUT: a library would
# otherwise be created with no sources on builds that enable the input
# subsystem but none of these.
if(CONFIG_INPUT_BB_TRACKBALL OR CONFIG_INPUT_ANALOG_STICK)
  add_subdirectory(input)
endif()

add_subdirectory_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_ACCELERATION input_processor)
