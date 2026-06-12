# GLSL 450 -> SPIR-V at build time. Primary path builds the pinned glslang from
# source (works on machines with zero preinstalled tools). Escape hatch:
# -DDS_USE_PREBUILT_SHADERS=ON copies the committed artifacts from shaders/compiled/.
#
# Outputs land in ${CMAKE_BINARY_DIR}/shaders/<name>.spv, next to the executable.

set(DS_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${DS_SHADER_OUT_DIR})

set(_ds_shader_outputs "")

function(ds_add_shader SOURCE_REL)
  get_filename_component(_name ${SOURCE_REL} NAME)
  set(_src ${CMAKE_SOURCE_DIR}/${SOURCE_REL})
  set(_out ${DS_SHADER_OUT_DIR}/${_name}.spv)

  if(DS_USE_PREBUILT_SHADERS)
    set(_prebuilt ${CMAKE_SOURCE_DIR}/shaders/compiled/${_name}.spv)
    if(NOT EXISTS ${_prebuilt})
      message(FATAL_ERROR "DS_USE_PREBUILT_SHADERS is ON but ${_prebuilt} is missing")
    endif()
    add_custom_command(
      OUTPUT ${_out}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_prebuilt} ${_out}
      DEPENDS ${_prebuilt}
      COMMENT "Copying prebuilt shader ${_name}.spv"
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT ${_out}
      COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.0 -o ${_out} ${_src}
      DEPENDS ${_src} glslang-standalone
      COMMENT "Compiling shader ${SOURCE_REL}"
      VERBATIM
    )
  endif()

  set(_ds_shader_outputs ${_ds_shader_outputs} ${_out} PARENT_SCOPE)
endfunction()

ds_add_shader(shaders/world.vert)
ds_add_shader(shaders/world.frag)
ds_add_shader(shaders/sprite.vert)
ds_add_shader(shaders/sprite.frag)

add_custom_target(shaders ALL DEPENDS ${_ds_shader_outputs})

# Convenience: refresh the committed fallback artifacts after a successful build.
add_custom_target(shaders_update_fallback
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${DS_SHADER_OUT_DIR} ${CMAKE_SOURCE_DIR}/shaders/compiled
  DEPENDS shaders
  COMMENT "Copying built .spv files into shaders/compiled/"
)
