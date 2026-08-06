# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-3.0-or-later

# deko3d accepts nothing but DKSH, so the GLSL this project generates has to be
# compiled by uam wherever it runs.

include(LsfgDependencies)

lsfg_require_uam()

find_program(LSFG_BISON NAMES bison)
find_program(LSFG_FLEX NAMES flex)
find_program(LSFG_PYTHON NAMES python3 python)
foreach(_tool LSFG_BISON LSFG_FLEX LSFG_PYTHON)
    if(NOT ${_tool})
        message(FATAL_ERROR "${_tool} not found. bison, flex, and python3 are needed to build uam. Configure with -DLSFG_BUILD_UAM=OFF to skip it.")
    endif()
endforeach()

set(_uam_gen "${CMAKE_BINARY_DIR}/uam-generated")
file(MAKE_DIRECTORY "${_uam_gen}/glsl/glcpp")

add_custom_command(
    OUTPUT "${_uam_gen}/glsl/glsl_parser.cpp" "${_uam_gen}/glsl/glsl_parser.h"
    COMMAND "${LSFG_BISON}" -o "${_uam_gen}/glsl/glsl_parser.cpp" -p _mesa_glsl_
            --defines="${_uam_gen}/glsl/glsl_parser.h" "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glsl_parser.yy"
    DEPENDS "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glsl_parser.yy"
    COMMENT "Generating uam GLSL parser"
)

add_custom_command(
    OUTPUT "${_uam_gen}/glsl/glsl_lexer.cpp"
    COMMAND "${LSFG_FLEX}" -o "${_uam_gen}/glsl/glsl_lexer.cpp"
            "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glsl_lexer.ll"
    DEPENDS "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glsl_lexer.ll"
    COMMENT "Generating uam GLSL lexer"
)

add_custom_command(
    OUTPUT "${_uam_gen}/glsl/glcpp/glcpp-parse.c" "${_uam_gen}/glsl/glcpp/glcpp-parse.h"
    COMMAND "${LSFG_BISON}" -o "${_uam_gen}/glsl/glcpp/glcpp-parse.c" -p glcpp_parser_
            --defines="${_uam_gen}/glsl/glcpp/glcpp-parse.h"
            "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glcpp/glcpp-parse.y"
    DEPENDS "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glcpp/glcpp-parse.y"
    COMMENT "Generating uam GLSL preprocessor parser"
)

add_custom_command(
    OUTPUT "${_uam_gen}/glsl/glcpp/glcpp-lex.c"
    COMMAND "${LSFG_FLEX}" -o "${_uam_gen}/glsl/glcpp/glcpp-lex.c"
            "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glcpp/glcpp-lex.l"
    DEPENDS "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/glcpp/glcpp-lex.l"
    COMMENT "Generating uam GLSL preprocessor lexer"
)

foreach(_variant enum constant strings)
    if(_variant STREQUAL "enum")
        set(_out "${_uam_gen}/glsl/ir_expression_operation.h")
    else()
        set(_out "${_uam_gen}/glsl/ir_expression_operation_${_variant}.h")
    endif()
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND "${LSFG_PYTHON}" "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/ir_expression_operation.py" ${_variant}
                > "${_out}"
        DEPENDS "${LSFG_UAM_SOURCE_DIR}/mesa-imported/glsl/ir_expression_operation.py"
        COMMENT "Generating uam ${_out}"
    )
    list(APPEND _uam_generated "${_out}")
endforeach()

list(
    APPEND _uam_generated
    "${_uam_gen}/glsl/glsl_parser.cpp"
    "${_uam_gen}/glsl/glsl_lexer.cpp"
    "${_uam_gen}/glsl/glcpp/glcpp-parse.c"
    "${_uam_gen}/glsl/glcpp/glcpp-lex.c"
)

set(_uam_sources
    source/compiler_iface.cpp
    source/glsl_frontend.cpp
    source/mini-os.c
    source/tgsi_support.cpp

    mesa-imported/codegen/nv50_ir.cpp
    mesa-imported/codegen/nv50_ir_bb.cpp
    mesa-imported/codegen/nv50_ir_build_util.cpp
    mesa-imported/codegen/nv50_ir_emit_gk110.cpp
    mesa-imported/codegen/nv50_ir_emit_gm107.cpp
    mesa-imported/codegen/nv50_ir_emit_nv50.cpp
    mesa-imported/codegen/nv50_ir_emit_nvc0.cpp
    mesa-imported/codegen/nv50_ir_from_tgsi.cpp
    mesa-imported/codegen/nv50_ir_graph.cpp
    mesa-imported/codegen/nv50_ir_lowering_gm107.cpp
    mesa-imported/codegen/nv50_ir_lowering_nv50.cpp
    mesa-imported/codegen/nv50_ir_lowering_nvc0.cpp
    mesa-imported/codegen/nv50_ir_peephole.cpp
    mesa-imported/codegen/nv50_ir_print.cpp
    mesa-imported/codegen/nv50_ir_ra.cpp
    mesa-imported/codegen/nv50_ir_ssa.cpp
    mesa-imported/codegen/nv50_ir_target.cpp
    mesa-imported/codegen/nv50_ir_target_gm107.cpp
    mesa-imported/codegen/nv50_ir_target_nv50.cpp
    mesa-imported/codegen/nv50_ir_target_nvc0.cpp
    mesa-imported/codegen/nv50_ir_util.cpp

    mesa-imported/compiler/blob.c
    mesa-imported/compiler/glsl_types.cpp
    mesa-imported/compiler/shader_enums.c

    mesa-imported/cso_cache/cso_cache.c
    mesa-imported/cso_cache/cso_hash.c

    mesa-imported/glsl/ast_array_index.cpp
    mesa-imported/glsl/ast_expr.cpp
    mesa-imported/glsl/ast_function.cpp
    mesa-imported/glsl/ast_to_hir.cpp
    mesa-imported/glsl/ast_type.cpp
    mesa-imported/glsl/builtin_functions.cpp
    mesa-imported/glsl/builtin_types.cpp
    mesa-imported/glsl/builtin_variables.cpp
    mesa-imported/glsl/generate_ir.cpp
    mesa-imported/glsl/glsl_parser_extras.cpp
    mesa-imported/glsl/glsl_symbol_table.cpp
    mesa-imported/glsl/hir_field_selection.cpp
    mesa-imported/glsl/ir.cpp
    mesa-imported/glsl/ir_array_refcount.cpp
    mesa-imported/glsl/ir_basic_block.cpp
    mesa-imported/glsl/ir_builder.cpp
    mesa-imported/glsl/ir_clone.cpp
    mesa-imported/glsl/ir_constant_expression.cpp
    mesa-imported/glsl/ir_equals.cpp
    mesa-imported/glsl/ir_expression_flattening.cpp
    mesa-imported/glsl/ir_function.cpp
    mesa-imported/glsl/ir_function_can_inline.cpp
    mesa-imported/glsl/ir_function_detect_recursion.cpp
    mesa-imported/glsl/ir_hierarchical_visitor.cpp
    mesa-imported/glsl/ir_hv_accept.cpp
    mesa-imported/glsl/ir_print_visitor.cpp
    mesa-imported/glsl/ir_reader.cpp
    mesa-imported/glsl/ir_rvalue_visitor.cpp
    mesa-imported/glsl/ir_set_program_inouts.cpp
    mesa-imported/glsl/ir_validate.cpp
    mesa-imported/glsl/ir_variable_refcount.cpp
    mesa-imported/glsl/link_atomics.cpp
    mesa-imported/glsl/link_functions.cpp
    mesa-imported/glsl/link_interface_blocks.cpp
    mesa-imported/glsl/link_uniform_block_active_visitor.cpp
    mesa-imported/glsl/link_uniform_blocks.cpp
    mesa-imported/glsl/link_uniform_initializers.cpp
    mesa-imported/glsl/link_uniforms.cpp
    mesa-imported/glsl/link_varyings.cpp
    mesa-imported/glsl/linker.cpp
    mesa-imported/glsl/linker_util.cpp
    mesa-imported/glsl/loop_analysis.cpp
    mesa-imported/glsl/loop_unroll.cpp
    mesa-imported/glsl/lower_blend_equation_advanced.cpp
    mesa-imported/glsl/lower_buffer_access.cpp
    mesa-imported/glsl/lower_const_arrays_to_uniforms.cpp
    mesa-imported/glsl/lower_cs_derived.cpp
    mesa-imported/glsl/lower_discard.cpp
    mesa-imported/glsl/lower_discard_flow.cpp
    mesa-imported/glsl/lower_distance.cpp
    mesa-imported/glsl/lower_if_to_cond_assign.cpp
    mesa-imported/glsl/lower_instructions.cpp
    mesa-imported/glsl/lower_int64.cpp
    mesa-imported/glsl/lower_jumps.cpp
    mesa-imported/glsl/lower_mat_op_to_vec.cpp
    mesa-imported/glsl/lower_named_interface_blocks.cpp
    mesa-imported/glsl/lower_noise.cpp
    mesa-imported/glsl/lower_offset_array.cpp
    mesa-imported/glsl/lower_output_reads.cpp
    mesa-imported/glsl/lower_packed_varyings.cpp
    mesa-imported/glsl/lower_packing_builtins.cpp
    mesa-imported/glsl/lower_shared_reference.cpp
    mesa-imported/glsl/lower_subroutine.cpp
    mesa-imported/glsl/lower_tess_level.cpp
    mesa-imported/glsl/lower_texture_projection.cpp
    mesa-imported/glsl/lower_ubo_reference.cpp
    mesa-imported/glsl/lower_variable_index_to_cond_assign.cpp
    mesa-imported/glsl/lower_vec_index_to_cond_assign.cpp
    mesa-imported/glsl/lower_vec_index_to_swizzle.cpp
    mesa-imported/glsl/lower_vector.cpp
    mesa-imported/glsl/lower_vector_derefs.cpp
    mesa-imported/glsl/lower_vector_insert.cpp
    mesa-imported/glsl/lower_vertex_id.cpp
    mesa-imported/glsl/opt_algebraic.cpp
    mesa-imported/glsl/opt_array_splitting.cpp
    mesa-imported/glsl/opt_conditional_discard.cpp
    mesa-imported/glsl/opt_constant_folding.cpp
    mesa-imported/glsl/opt_constant_propagation.cpp
    mesa-imported/glsl/opt_constant_variable.cpp
    mesa-imported/glsl/opt_copy_propagation_elements.cpp
    mesa-imported/glsl/opt_dead_builtin_variables.cpp
    mesa-imported/glsl/opt_dead_builtin_varyings.cpp
    mesa-imported/glsl/opt_dead_code.cpp
    mesa-imported/glsl/opt_dead_code_local.cpp
    mesa-imported/glsl/opt_dead_functions.cpp
    mesa-imported/glsl/opt_flatten_nested_if_blocks.cpp
    mesa-imported/glsl/opt_flip_matrices.cpp
    mesa-imported/glsl/opt_function_inlining.cpp
    mesa-imported/glsl/opt_if_simplification.cpp
    mesa-imported/glsl/opt_minmax.cpp
    mesa-imported/glsl/opt_rebalance_tree.cpp
    mesa-imported/glsl/opt_redundant_jumps.cpp
    mesa-imported/glsl/opt_structure_splitting.cpp
    mesa-imported/glsl/opt_swizzle.cpp
    mesa-imported/glsl/opt_tree_grafting.cpp
    mesa-imported/glsl/opt_vectorize.cpp
    mesa-imported/glsl/propagate_invariance.cpp
    mesa-imported/glsl/s_expression.cpp
    mesa-imported/glsl/serialize.cpp
    mesa-imported/glsl/standalone_scaffolding.cpp
    mesa-imported/glsl/string_to_uint_map.cpp
    mesa-imported/glsl/glcpp/pp.c

    mesa-imported/main/imports.c
    mesa-imported/main/shaderimage.c
    mesa-imported/main/uniform_query.cpp
    mesa-imported/main/uniforms.c

    mesa-imported/program/ir_to_mesa.cpp
    mesa-imported/program/prog_instruction.c
    mesa-imported/program/prog_parameter.c
    mesa-imported/program/symbol_table.c

    mesa-imported/state_tracker/st_format.c
    mesa-imported/state_tracker/st_glsl_to_tgsi.cpp
    mesa-imported/state_tracker/st_glsl_to_tgsi_array_merge.cpp
    mesa-imported/state_tracker/st_glsl_to_tgsi_private.cpp
    mesa-imported/state_tracker/st_glsl_to_tgsi_temprename.cpp
    mesa-imported/state_tracker/st_glsl_types.cpp

    mesa-imported/tgsi/tgsi_aa_point.c
    mesa-imported/tgsi/tgsi_build.c
    mesa-imported/tgsi/tgsi_dump.c
    mesa-imported/tgsi/tgsi_emulate.c
    mesa-imported/tgsi/tgsi_from_mesa.c
    mesa-imported/tgsi/tgsi_info.c
    mesa-imported/tgsi/tgsi_iterate.c
    mesa-imported/tgsi/tgsi_lowering.c
    mesa-imported/tgsi/tgsi_parse.c
    mesa-imported/tgsi/tgsi_point_sprite.c
    mesa-imported/tgsi/tgsi_sanity.c
    mesa-imported/tgsi/tgsi_scan.c
    mesa-imported/tgsi/tgsi_strings.c
    mesa-imported/tgsi/tgsi_text.c
    mesa-imported/tgsi/tgsi_transform.c
    mesa-imported/tgsi/tgsi_two_side.c
    mesa-imported/tgsi/tgsi_ureg.c
    mesa-imported/tgsi/tgsi_util.c

    mesa-imported/util/bitscan.c
    mesa-imported/util/half_float.c
    mesa-imported/util/hash_table.c
    mesa-imported/util/ralloc.c
    mesa-imported/util/set.c
    mesa-imported/util/string_buffer.c
    mesa-imported/util/strtod.c
    mesa-imported/util/u_bitmask.c
    mesa-imported/util/u_debug.c
    mesa-imported/util/u_format.c
)
list(TRANSFORM _uam_sources PREPEND "${LSFG_UAM_SOURCE_DIR}/")

set(_uam_bridge_dir "${PROJECT_SOURCE_DIR}/third_party/uam-bridge")
set(_uam_stderr "${_uam_bridge_dir}/UamStderr.h")

add_library(lsfg_uam_objects STATIC ${_uam_sources} ${_uam_generated} "${_uam_bridge_dir}/UamBridge.cpp")

target_include_directories(
    lsfg_uam_objects
    PRIVATE
        "${_uam_gen}"
        "${_uam_gen}/glsl"
        "${LSFG_UAM_SOURCE_DIR}/mesa-imported"
        "${LSFG_UAM_SOURCE_DIR}/source"
        "${_uam_bridge_dir}"
)

# mesa picks its aligned allocator from these.
include(CheckSymbolExists)
block()
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(posix_memalign "stdlib.h" LSFG_HAVE_POSIX_MEMALIGN)
endblock()

target_compile_definitions(
    lsfg_uam_objects
    PRIVATE
        PACKAGE_STRING="uam"
        DESKTOP
        _USE_MATH_DEFINES
        _GNU_SOURCE
        NDEBUG
        $<$<BOOL:${LSFG_HAVE_POSIX_MEMALIGN}>:HAVE_POSIX_MEMALIGN>
)

# mesa 19.0 predates C++14 and screams warnings under it.
set_target_properties(
    lsfg_uam_objects
    PROPERTIES
        C_STANDARD 99
        C_EXTENSIONS ON
        CXX_STANDARD 11
        CXX_EXTENSIONS ON
        INTERPROCEDURAL_OPTIMIZATION OFF
        POSITION_INDEPENDENT_CODE ON
)
target_compile_options(lsfg_uam_objects PRIVATE -w -fno-lto -include "${_uam_stderr}")

set_source_files_properties(
    "${LSFG_UAM_SOURCE_DIR}/source/compiler_iface.cpp"
    PROPERTIES COMPILE_DEFINITIONS "glsl_frontend_init=UamFrontendInitOnce;glsl_frontend_exit=UamFrontendExitOnce"
)

set(_uam_isolated "${_uam_gen}/uam_isolated.o")
add_custom_command(
    OUTPUT "${_uam_isolated}"
    COMMAND "${CMAKE_LINKER}" -r --force-group-allocation
            --whole-archive "$<TARGET_FILE:lsfg_uam_objects>" --no-whole-archive
            -o "${_uam_isolated}.tmp"
    COMMAND "${CMAKE_OBJCOPY}" --keep-global-symbol=UamCompileGlsl "${_uam_isolated}.tmp" "${_uam_isolated}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${_uam_isolated}.tmp"
    DEPENDS lsfg_uam_objects
    COMMENT "Localising uam's mesa symbols"
)
add_custom_target(lsfg_uam_isolate DEPENDS "${_uam_isolated}")

add_library(lsfg_uam INTERFACE)
add_library(lsfg::uam ALIAS lsfg_uam)
add_dependencies(lsfg_uam lsfg_uam_isolate)
target_include_directories(lsfg_uam INTERFACE "${_uam_bridge_dir}")
target_link_libraries(lsfg_uam INTERFACE "${_uam_isolated}")
