# cmake -P ValidateComponentIndex.cmake -DINDEX_DIR=<dir> [-DOUTPUT=<file>]
#                                        [-DBASE_INDEX=<file>]
#
# Per-header generation cannot see a collision with another header. This reads
# every index sidecar and fails on a stable identity, schema name or scene chunk
# claimed twice -- naming both declarations, since either one could be the
# mistake.
#
# OUTPUT receives the sidecars' records as one sorted index: the target's
# component roster, which its tests compare against what it registers and which
# the SDK installs. BASE_INDEX is the engine's installed index, so an
# out-of-tree game module is checked against engine components it cannot see;
# it is validated against, never copied into OUTPUT.

# A component may be authored without being persisted, so a record's scene
# chunk is legitimately empty; without this the field would vanish from the
# split and the record would read as malformed.
cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED INDEX_DIR)
    message(FATAL_ERROR "ValidateComponentIndex: INDEX_DIR is required")
endif()

file(GLOB_RECURSE _index_files "${INDEX_DIR}/*.index")
list(SORT _index_files)

set(_own_records "")
foreach(_file IN LISTS _index_files)
    file(STRINGS "${_file}" _lines)
    list(APPEND _own_records ${_lines})
endforeach()
list(SORT _own_records)

# The base claims come first, so a module's collision is reported against the
# engine declaration it cannot change.
set(_base_records "")
if(DEFINED BASE_INDEX AND EXISTS "${BASE_INDEX}")
    file(STRINGS "${BASE_INDEX}" _base_records)
endif()

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

foreach(_line IN LISTS _base_records _own_records)
    string(REPLACE "\t" ";" _record "${_line}")
    list(LENGTH _record _count)
    if(NOT _count EQUAL 6)
        list(APPEND _errors "error: malformed index record '${_line}'")
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

if(_errors)
    foreach(_error IN LISTS _errors)
        message(SEND_ERROR "${_error}")
    endforeach()
    message(FATAL_ERROR "component metadata has colliding declarations")
endif()

if(DEFINED OUTPUT)
    list(JOIN _own_records "\n" _content)
    if(_content)
        string(APPEND _content "\n")
    endif()
    file(WRITE "${OUTPUT}" "${_content}")
endif()
