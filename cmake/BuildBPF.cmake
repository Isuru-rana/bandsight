# SPDX-License-Identifier: GPL-3.0-or-later
# Compiles a .bpf.c to BPF bytecode, then generates a libbpf skeleton header.
function(bandsight_add_bpf target source)
  get_filename_component(stem ${source} NAME_WE)          # bandsight.bpf -> stem
  string(REPLACE ".bpf" "" stem ${stem})
  set(obj  ${CMAKE_CURRENT_BINARY_DIR}/${stem}.bpf.o)
  set(skel ${CMAKE_CURRENT_BINARY_DIR}/${stem}.skel.h)

  execute_process(COMMAND uname -m OUTPUT_VARIABLE arch OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(arch STREQUAL "x86_64")
    set(bpf_arch x86)
  elseif(arch MATCHES "aarch64")
    set(bpf_arch arm64)
  else()
    message(FATAL_ERROR "Unsupported architecture for BPF build: ${arch}")
  endif()

  add_custom_command(
    OUTPUT ${obj}
    COMMAND clang -g -O2 -target bpf -D__TARGET_ARCH_${bpf_arch}
                  -I${CMAKE_SOURCE_DIR}/bpf
                  -c ${CMAKE_SOURCE_DIR}/${source} -o ${obj}
    DEPENDS ${CMAKE_SOURCE_DIR}/${source} ${CMAKE_SOURCE_DIR}/bpf/vmlinux.h
    COMMENT "Compiling BPF object ${stem}.bpf.o"
    VERBATIM)

  add_custom_command(
    OUTPUT ${skel}
    COMMAND bpftool gen skeleton ${obj} > ${skel}
    DEPENDS ${obj}
    COMMENT "Generating BPF skeleton ${stem}.skel.h"
    VERBATIM)

  add_custom_target(${target} DEPENDS ${skel})
  set(${target}_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
endfunction()
