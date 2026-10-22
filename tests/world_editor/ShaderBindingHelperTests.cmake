cmake_minimum_required(VERSION 3.25)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${REPO_ROOT}/cmake/ValidateShaderResourceBindings.cmake")

set(reflection [=[
{
  "separate_images": [
    {"set": 2, "binding": 3},
    {"set": 2, "binding": 1},
    {"set": 2, "binding": 0},
    {"set": 2, "binding": 2}
  ],
  "ubos": [
    {"set": 3, "binding": 1},
    {"set": 3, "binding": 0}
  ]
}
]=])

mu_check_shader_binding_set("${reflection}" separate_images 4 2 image_errors)
if(image_errors)
    message(FATAL_ERROR "valid image binding set rejected: ${image_errors}")
endif()

mu_check_shader_binding_set("${reflection}" ubos 2 3 uniform_errors)
if(uniform_errors)
    message(FATAL_ERROR "valid uniform binding set rejected: ${uniform_errors}")
endif()

mu_check_shader_binding_set("${reflection}" separate_images 4 3 wrong_set_errors)
if(NOT wrong_set_errors)
    message(FATAL_ERROR "wrong descriptor set was not rejected")
endif()

set(gapped [=[
{
  "separate_images": [
    {"set": 2, "binding": 0},
    {"set": 2, "binding": 2}
  ]
}
]=])
mu_check_shader_binding_set("${gapped}" separate_images 2 2 gap_errors)
if(NOT gap_errors)
    message(FATAL_ERROR "gapped binding set was not rejected")
endif()

message(STATUS "Validated shader binding helper with unordered valid resources and negative cases")
