include_guard(GLOBAL)

function(cler_register_block_component)
  cmake_parse_arguments(ARG "" "TARGET" "PATHS" ${ARGN})
  if(NOT ARG_TARGET OR NOT TARGET ${ARG_TARGET})
    message(FATAL_ERROR "cler_register_block_component requires an existing TARGET")
  endif()

  foreach(path IN LISTS ARG_PATHS)
    if(IS_ABSOLUTE "${path}")
      get_filename_component(absolute "${path}" REALPATH)
    else()
      get_filename_component(absolute "${PROJECT_SOURCE_DIR}/${path}" REALPATH)
    endif()
    set_property(GLOBAL APPEND PROPERTY CLER_BLOCK_COMPONENT_ORIGINS "${absolute}")
    set_property(GLOBAL APPEND PROPERTY CLER_BLOCK_COMPONENT_TARGETS "${ARG_TARGET}")
  endforeach()
endfunction()

function(cler_configure_editor_exact_block_linking)
  cmake_parse_arguments(ARG "" "TARGET;REQUIREMENTS_FILE" "" ${ARGN})
  if(NOT ARG_TARGET OR NOT TARGET ${ARG_TARGET})
    message(FATAL_ERROR "CLER editor target '${ARG_TARGET}' does not exist")
  endif()
  if(NOT ARG_REQUIREMENTS_FILE OR NOT EXISTS "${ARG_REQUIREMENTS_FILE}")
    message(FATAL_ERROR "CLER editor requirements file does not exist: '${ARG_REQUIREMENTS_FILE}'")
  endif()

  get_property(link_libraries TARGET ${ARG_TARGET} PROPERTY LINK_LIBRARIES)
  list(REMOVE_ITEM link_libraries cler::desktop_blocks cler_desktop_blocks)
  set_property(TARGET ${ARG_TARGET} PROPERTY LINK_LIBRARIES "${link_libraries}")

  get_property(registered_origins GLOBAL PROPERTY CLER_BLOCK_COMPONENT_ORIGINS)
  get_property(registered_targets GLOBAL PROPERTY CLER_BLOCK_COMPONENT_TARGETS)
  file(STRINGS "${ARG_REQUIREMENTS_FILE}" requested_origins)

  set(components cler::cler)
  get_target_property(explicit_components ${ARG_TARGET} CLER_EDITOR_COMPONENTS)
  if(explicit_components AND NOT explicit_components STREQUAL "explicit_components-NOTFOUND")
    foreach(component IN LISTS explicit_components)
      if(NOT TARGET ${component})
        message(FATAL_ERROR "CLER editor component '${component}' for target '${ARG_TARGET}' does not exist")
      endif()
      list(APPEND components "${component}")
    endforeach()
  endif()
  set(unresolved)
  foreach(origin IN LISTS requested_origins)
    string(STRIP "${origin}" origin)
    if(origin STREQUAL "" OR origin MATCHES "^#")
      continue()
    endif()
    if(IS_ABSOLUTE "${origin}")
      get_filename_component(absolute "${origin}" REALPATH)
    else()
      get_filename_component(absolute "${PROJECT_SOURCE_DIR}/${origin}" REALPATH)
    endif()
    list(FIND registered_origins "${absolute}" index)
    if(index EQUAL -1)
      list(APPEND unresolved "${origin}")
    else()
      list(GET registered_targets ${index} component)
      list(APPEND components "${component}")
    endif()
  endforeach()

  if(unresolved)
    target_link_libraries(${ARG_TARGET} PRIVATE cler::desktop_blocks)
    set_property(TARGET ${ARG_TARGET} PROPERTY CLER_EDITOR_BLOCK_LINK_MODE umbrella)
    set_property(TARGET ${ARG_TARGET} PROPERTY CLER_EDITOR_BLOCK_UNRESOLVED_ORIGINS "${unresolved}")
    message(STATUS "CLER editor target '${ARG_TARGET}' block linking: umbrella fallback (unknown origins: ${unresolved})")
  else()
    list(REMOVE_DUPLICATES components)
    target_link_libraries(${ARG_TARGET} PRIVATE ${components})
    set_property(TARGET ${ARG_TARGET} PROPERTY CLER_EDITOR_BLOCK_LINK_MODE exact)
    set_property(TARGET ${ARG_TARGET} PROPERTY CLER_EDITOR_BLOCK_COMPONENTS "${components}")
    message(STATUS "CLER editor target '${ARG_TARGET}' block linking: exact (${components})")
  endif()
endfunction()

function(cler_register_standard_block_components)
  cler_register_block_component(TARGET cler::blocks_blob PATHS desktop_blocks/blob.hpp)
  cler_register_block_component(TARGET cler::blocks_core PATHS
    desktop_blocks/kernels/kernels.hpp
    desktop_blocks/math/add.hpp
    desktop_blocks/math/complex_demux.hpp
    desktop_blocks/math/frequency_shift.hpp
    desktop_blocks/math/gain.hpp
    desktop_blocks/misc/uhd_common.hpp
    desktop_blocks/noise/awgn.hpp
    desktop_blocks/utils/fanout.hpp
    desktop_blocks/utils/gate.hpp
    desktop_blocks/utils/fused.hpp
    desktop_blocks/utils/throttle.hpp
    desktop_blocks/utils/throughput.hpp
  )
  cler_register_block_component(TARGET cler::blocks_sigmf PATHS
    desktop_blocks/sigmf/sigmf.hpp
    desktop_blocks/sigmf/source_sigmf.hpp
    desktop_blocks/sigmf/sink_sigmf.hpp
  )
  cler_register_block_component(TARGET cler::blocks_udp PATHS
    desktop_blocks/udp/shared.hpp
    desktop_blocks/udp/source_udp.hpp
    desktop_blocks/udp/sink_udp.hpp
  )

  if(TARGET cler::blocks_web)
    cler_register_block_component(TARGET cler::blocks_web PATHS
      desktop_blocks/web/json_sink.hpp
      desktop_blocks/web/web_sink.hpp
    )
  endif()

  if(CLER_BUILD_BLOCKS_LIQUID)
    cler_register_block_component(TARGET cler::blocks_liquid PATHS
      desktop_blocks/channelizers/polyphase_analyzer.hpp
      desktop_blocks/channelizers/polyphase_channelizer.hpp
      desktop_blocks/channelizers/polyphase_transform_5.hpp
      desktop_blocks/filters/kaiser_lpf.hpp
      desktop_blocks/fm/fm_demod.hpp
      desktop_blocks/fm/fm_mpx_decoder.hpp
      desktop_blocks/fm/rds.hpp
      desktop_blocks/resamplers/multistage_resampler.hpp
      desktop_blocks/resamplers/rational_resampler.hpp
      desktop_blocks/spectrum/spectrum.hpp
    )
    cler_register_block_component(TARGET cler::blocks_linear_modem PATHS
      desktop_blocks/linear_modem/ber_counter.hpp
      desktop_blocks/linear_modem/demodulator.hpp
      desktop_blocks/linear_modem/modulator.hpp
      desktop_blocks/linear_modem/symbol_source.hpp
    )
    cler_register_block_component(TARGET cler::blocks_demod PATHS
      desktop_blocks/demod/analog_demod.hpp
    )
    cler_register_block_component(TARGET cler::blocks_ais PATHS
      desktop_blocks/ais/ais.hpp
      desktop_blocks/ais/ais_decoder.hpp
    )
    cler_register_block_component(TARGET cler::blocks_aprs PATHS
      desktop_blocks/aprs/aprs.hpp
      desktop_blocks/aprs/afsk_demod.hpp
    )
    cler_register_block_component(TARGET cler::blocks_fec PATHS
      desktop_blocks/fec/deframer.hpp
      desktop_blocks/fec/fec.hpp
      desktop_blocks/fec/fec_decoder.hpp
      desktop_blocks/fec/fec_encoder.hpp
      desktop_blocks/fec/framer.hpp
    )
    cler_register_block_component(TARGET cler::blocks_ezgmsk PATHS
      desktop_blocks/ezgmsk/_ezgmsk_demod.h
      desktop_blocks/ezgmsk/_ezgmsk_mod.h
      desktop_blocks/ezgmsk/ezgmsk_demod.hpp
      desktop_blocks/ezgmsk/ezgmsk_mod.hpp
    )
  endif()

  if(CLER_BUILD_BLOCKS_GUI)
    cler_register_block_component(TARGET cler::blocks_ais_map PATHS desktop_blocks/ais/ais_map.hpp)
    cler_register_block_component(TARGET cler::blocks_aprs_map PATHS desktop_blocks/aprs/aprs_map.hpp)
    cler_register_block_component(TARGET cler::blocks_gui PATHS
      desktop_blocks/gui/gui_manager.hpp
      desktop_blocks/gui/coastline_loader.hpp
      desktop_blocks/gui/map_canvas.hpp
      desktop_blocks/triggers/trigger_block.hpp
    )
    cler_register_block_component(TARGET cler::blocks_plot_timeseries PATHS desktop_blocks/plots/plot_timeseries.hpp)
    if(CLER_BUILD_BLOCKS_LIQUID)
      cler_register_block_component(TARGET cler::blocks_linear_modem_plot PATHS desktop_blocks/linear_modem/plot_constellation.hpp)
    endif()
    if(CLER_BUILD_BLOCKS_LIQUID)
      cler_register_block_component(TARGET cler::blocks_plots_spectral PATHS
        desktop_blocks/plots/plot_cspectrum.hpp
        desktop_blocks/plots/plot_cspectrogram.hpp
        desktop_blocks/plots/spectral_windows.hpp
      )
    endif()
    cler_register_block_component(TARGET cler::blocks_adsb PATHS
      desktop_blocks/adsb/adsb_aggregate.hpp
      desktop_blocks/adsb/adsb_coastline_loader.hpp
      desktop_blocks/adsb/adsb_decoder.hpp
      desktop_blocks/adsb/adsb_types.hpp
      desktop_blocks/adsb/cpr.h
      desktop_blocks/adsb/modes.h
      desktop_blocks/adsb/modes_2400.h
    )
  endif()

  cler_register_block_component(TARGET cler::blocks_sources_core PATHS
    desktop_blocks/sources/source_chirp.hpp
    desktop_blocks/sources/source_cw.hpp
    desktop_blocks/sources/source_file.hpp
    desktop_blocks/sources/source_sim.hpp
  )
  cler_register_block_component(TARGET cler::blocks_sinks_core PATHS
    desktop_blocks/sinks/sink_file.hpp
    desktop_blocks/sinks/sink_null.hpp
  )
  if(TARGET cler::blocks_source_ffmpeg)
    cler_register_block_component(TARGET cler::blocks_source_ffmpeg PATHS desktop_blocks/sources/source_audio_file.hpp)
  endif()
  if(TARGET cler::blocks_source_cariboulite)
    cler_register_block_component(TARGET cler::blocks_source_cariboulite PATHS desktop_blocks/sources/source_cariboulite.hpp)
  endif()
  if(TARGET cler::blocks_hackrf)
    cler_register_block_component(TARGET cler::blocks_hackrf PATHS
      desktop_blocks/sources/source_hackrf.hpp
      desktop_blocks/sinks/sink_hackrf.hpp
    )
  endif()
  if(TARGET cler::blocks_source_pluto)
    cler_register_block_component(TARGET cler::blocks_source_pluto PATHS desktop_blocks/sources/source_pluto.hpp)
  endif()
  cler_register_block_component(TARGET cler::blocks_source_mux PATHS desktop_blocks/sources/source_mux.hpp)
  if(TARGET cler::blocks_soapysdr)
    cler_register_block_component(TARGET cler::blocks_soapysdr PATHS
      desktop_blocks/sources/source_soapysdr.hpp
      desktop_blocks/sinks/sink_soapysdr.hpp
    )
  endif()
  if(TARGET cler::blocks_uhd)
    cler_register_block_component(TARGET cler::blocks_uhd PATHS
      desktop_blocks/sources/source_uhd.hpp
      desktop_blocks/sinks/sink_uhd.hpp
    )
  endif()
  if(TARGET cler::blocks_sink_audio)
    cler_register_block_component(TARGET cler::blocks_sink_audio PATHS desktop_blocks/sinks/sink_audio.hpp)
  endif()
endfunction()

function(cler_configure_editor_target)
  if(CLER_EDITOR_SOURCE AND CLER_EDITOR_TARGET AND NOT TARGET ${CLER_EDITOR_TARGET})
    if(NOT CLER_BUILD_BLOCKS)
      message(FATAL_ERROR "CLER editor drafts outside the tree require CLER_BUILD_BLOCKS=ON")
    endif()
    add_executable(${CLER_EDITOR_TARGET} "${CLER_EDITOR_SOURCE}")
    message(STATUS "CLER editor draft '${CLER_EDITOR_TARGET}' from ${CLER_EDITOR_SOURCE}")
  endif()

  if(CLER_EDITOR_REQUIREMENTS_FILE AND CLER_EDITOR_REQUIREMENTS_EXACT)
    if(NOT CLER_BUILD_BLOCKS)
      message(FATAL_ERROR "CLER editor exact block linking requires CLER_BUILD_BLOCKS=ON")
    endif()
    if(NOT CLER_EDITOR_TARGET)
      message(FATAL_ERROR "CLER_EDITOR_REQUIREMENTS_FILE requires CLER_EDITOR_TARGET")
    endif()
    cler_configure_editor_exact_block_linking(
      TARGET "${CLER_EDITOR_TARGET}"
      REQUIREMENTS_FILE "${CLER_EDITOR_REQUIREMENTS_FILE}"
    )
  elseif(CLER_EDITOR_TARGET)
    if(TARGET ${CLER_EDITOR_TARGET})
      target_link_libraries(${CLER_EDITOR_TARGET} PRIVATE cler::desktop_blocks)
      set_property(TARGET ${CLER_EDITOR_TARGET} PROPERTY CLER_EDITOR_BLOCK_LINK_MODE umbrella)
      if(CLER_EDITOR_REQUIREMENTS_FILE)
        message(STATUS "CLER editor target '${CLER_EDITOR_TARGET}' block linking: umbrella fallback (incomplete requirements)")
      else()
        message(STATUS "CLER editor target '${CLER_EDITOR_TARGET}' block linking: umbrella (no requirements file)")
      endif()
    else()
      message(FATAL_ERROR "CLER editor target '${CLER_EDITOR_TARGET}' does not exist")
    endif()
  endif()
endfunction()
