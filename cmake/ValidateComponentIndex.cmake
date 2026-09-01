# cmake -P ValidateComponentIndex.cmake -DINDEX_DIR=<dir> [-DBASE_INDEX=<file>]
#
# Per-header generation cannot see a collision with another header. This reads
# every index sidecar and fails on a stable identity, schema name or scene chunk
# claimed twice -- naming both declarations, since either one could be the
# mistake.
#
# BASE_INDEX is the engine's installed index, so an out-of-tree game module is
# checked against engine components it cannot see.

# A component may be authored without being persisted, so a record's scene
# chunk is legitimately empty; without this the field would vanish from the
# split and the record would read as malformed.
cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED INDEX_DIR)
    message(FATAL_ERROR "ValidateComponentIndex: INDEX_DIR is required")
endif()

file(GLOB_RECURSE _index_files "${INDEX_DIR}/*.index")
if(DEFINED BASE_INDEX AND EXISTS "${BASE_INDEX}")
    list(APPEND _index_files "${BASE_INDEX}")
endif()
list(SORT _index_files)

set(_errors "")

# Each record is: Type \t Identity \t SchemaName \t SceneChunk \t Header \t Line
function(claim facet value type header line)
    if(value STREQUAL "")
        return()
    endif()
    string(MAKE_C_IDENTIFIER "${facet}_${value}" _key)
    if(DEFINED CLAIMED_${_key})
        list(APPEND _errors
            "${header}:${line}: error: ${facet} '${value}' is already claimed by ${CLAIMED_${_key}}")
        set(_errors "${_errors}" PARENT_SCOPE)
    else()
        set(CLAIMED_${_key} "${type} (${header}:${line})" PARENT_SCOPE)
    endif()
endfunction()

foreach(_file IN LISTS _index_files)
    file(STRINGS "${_file}" _lines)
    foreach(_line IN LISTS _lines)
        string(REPLACE "\t" ";" _record "${_line}")
        list(LENGTH _record _count)
        if(NOT _count EQUAL 6)
            list(APPEND _errors "${_file}: error: malformed index record '${_line}'")
            continue()
        endif()
        list(GET _record 0 _type)
        list(GET _record 1 _identity)
        list(GET _record 2 _schema)
        list(GET _record 3 _chunk)
        list(GET _record 4 _header)
        list(GET _record 5 _line_no)

        claim("identity" "${_identity}" "${_type}" "${_header}" "${_line_no}")
        claim("schema name" "${_schema}" "${_type}" "${_header}" "${_line_no}")
        claim("scene chunk" "${_chunk}" "${_type}" "${_header}" "${_line_no}")
    endforeach()
endforeach()

if(_errors)
    foreach(_error IN LISTS _errors)
        message(SEND_ERROR "${_error}")
    endforeach()
    message(FATAL_ERROR "component metadata has colliding declarations")
endif()
