# Enforces the transport firewall: platform sockets are an implementation detail
# of the net module and must not leak anywhere else.
#
#   - No engine file outside engine/src/net/ may include a socket header
#     (<sys/socket.h>, <netinet/...>, <arpa/...>, <winsock2.h>, <ws2tcpip.h>)
#     or name a platform socket type (sockaddr, SOCKET, recvfrom, sendto).
#   - Public net headers (engine/include/net/) are held to this too. The seam
#     exists so the protocol, channel, and session layers can be written and
#     tested without a socket in sight; a socket type in a public header would
#     put one back in every consumer.
#
# The same shape as CheckPhysicsIsolation.cmake, for the same reason: a backend
# that is only conventionally contained is one careless include from being
# everywhere.
#
# When crypto lands, libsodium joins this list on the same terms.
#
# Run standalone (no build needed):
#   cmake -P cmake/CheckNetIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Files/dirs allowed to touch platform sockets. Adding one is a conscious
# decision that belongs here, on the record.
set(NET_ALLOWED
    "/engine/src/net/"
)

set(VIOLATIONS "")

file(GLOB_RECURSE ENGINE_FILES
    "${REPO}/engine/include/*"
    "${REPO}/engine/src/*"
)
foreach(file ${ENGINE_FILES})
    set(allowed FALSE)
    foreach(ok ${NET_ALLOWED})
        if(file MATCHES "${ok}")
            set(allowed TRUE)
            break()
        endif()
    endforeach()
    if(allowed)
        continue()
    endif()

    file(READ "${file}" content)
    file(RELATIVE_PATH rel "${REPO}" "${file}")

    string(REGEX MATCH
        "#[ \t]*include[ \t]*[<\"](sys/socket|netinet/|arpa/|winsock2|ws2tcpip|netdb)"
        inc_hit "${content}")
    if(inc_hit)
        list(APPEND VIOLATIONS
            "${rel} includes a socket header (sockets must stay behind engine/src/net/)")
    endif()

    # sockaddr and the send/recv verbs are the types a leak actually shows up
    # as. Matched with a word boundary so a comment mentioning "socket" in prose
    # does not trip the check.
    string(REGEX MATCH "\\b(sockaddr|sockaddr_in|sockaddr_in6|recvfrom|sendto|WSAStartup)\\b"
        type_hit "${content}")
    if(type_hit)
        list(APPEND VIOLATIONS
            "${rel} names a platform socket type or call (${type_hit}); it belongs behind the transport seam")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "net isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "net isolation check failed: ${n} violation(s)")
endif()

message(STATUS "net isolation OK (sockets confined to engine/src/net/)")
