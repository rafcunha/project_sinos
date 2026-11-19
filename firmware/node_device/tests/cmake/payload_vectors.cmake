# Turns tests/fixtures/payload_vectors.json into a C header for test_payload.c,
# so the C codec and the TTN formatter (ttn/uplink_formatter.test.js) are tested
# against the same vectors. Uses CMake's own JSON parser (no Python needed).
#
# payload_vectors_generate(<json> <out_header>)

set(PV_MAX_FIELDS 12)

function(payload_vectors_generate json_file out_header)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${json_file})
    file(READ ${json_file} js)

    set(out "/* Generated from fixtures/payload_vectors.json by cmake/payload_vectors.cmake. Do not edit. */\n")
    string(APPEND out "#ifndef PAYLOAD_VECTORS_H\n#define PAYLOAD_VECTORS_H\n\n")
    string(APPEND out "#define PV_MAX_FIELDS ${PV_MAX_FIELDS}\n\n")
    string(APPEND out "typedef struct { const char *key; long value; } pv_field_t;\n")
    string(APPEND out "typedef struct {\n    const char *name;\n    const char *hex;\n    int roundtrip;\n    int nfields;\n    pv_field_t fields[PV_MAX_FIELDS];\n} pv_valid_t;\n")
    string(APPEND out "typedef struct { const char *name; const char *hex; const char *error; } pv_invalid_t;\n\n")

    # ---- valid ----
    string(JSON n LENGTH "${js}" valid)
    math(EXPR last "${n} - 1")
    string(APPEND out "static const pv_valid_t PV_VALID[] = {\n")
    foreach(i RANGE ${last})
        string(JSON name GET "${js}" valid ${i} name)
        string(JSON hex GET "${js}" valid ${i} hex)
        string(JSON rt ERROR_VARIABLE rt_err GET "${js}" valid ${i} roundtrip)
        if(rt_err OR rt)
            set(rt 1)
        else()
            set(rt 0)
        endif()
        string(JSON nf LENGTH "${js}" valid ${i} fields)
        if(nf GREATER PV_MAX_FIELDS)
            message(FATAL_ERROR "payload_vectors: '${name}' has more than ${PV_MAX_FIELDS} fields")
        endif()
        math(EXPR flast "${nf} - 1")
        set(fields "")
        foreach(j RANGE ${flast})
            string(JSON key MEMBER "${js}" valid ${i} fields ${j})
            string(JSON val GET "${js}" valid ${i} fields ${key})
            string(APPEND fields "{\"${key}\", ${val}L}, ")
        endforeach()
        string(APPEND out "    {\"${name}\", \"${hex}\", ${rt}, ${nf}, {${fields}}},\n")
    endforeach()
    string(APPEND out "};\n\n")

    # ---- invalid ----
    string(JSON n LENGTH "${js}" invalid)
    math(EXPR last "${n} - 1")
    string(APPEND out "static const pv_invalid_t PV_INVALID[] = {\n")
    foreach(i RANGE ${last})
        string(JSON name GET "${js}" invalid ${i} name)
        string(JSON hex GET "${js}" invalid ${i} hex)
        string(JSON err GET "${js}" invalid ${i} error)
        string(APPEND out "    {\"${name}\", \"${hex}\", \"${err}\"},\n")
    endforeach()
    string(APPEND out "};\n\n#endif /* PAYLOAD_VECTORS_H */\n")

    # Write only on change: no needless rebuilds.
    if(EXISTS ${out_header})
        file(READ ${out_header} old)
    else()
        set(old "")
    endif()
    if(NOT old STREQUAL out)
        file(WRITE ${out_header} "${out}")
    endif()
endfunction()
