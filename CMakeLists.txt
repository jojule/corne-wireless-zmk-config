if(CONFIG_CORNE_UTILITY)
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-display-data.py --check
    RESULT_VARIABLE legend_check
  )
  if(NOT legend_check EQUAL 0)
    message(FATAL_ERROR "Display legends are stale; run scripts/generate-display-data.py")
  endif()
  zephyr_library()
  zephyr_library_include_directories(include ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(
    src/utility/keymap_data.c
    src/utility/lookup.c
    src/utility/render.c
    src/utility/screen.c
    src/utility/state.c
  )
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    # Observe original physical events before hold-tap/combo capture. Their
    # replayed events deliberately bypass this typed event-raiser wrapper.
    zephyr_link_libraries(-Wl,--wrap=raise_zmk_position_state_changed)
  endif()
endif()
