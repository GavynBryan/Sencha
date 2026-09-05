# The turret sample: networked possession of a placed object. Delete this
# directory and the include of this file to remove it.
list(APPEND SENCHA_GAME_MODULE_SOURCES
    samples/turret/TurretControl.cpp
    samples/turret/TurretSample.cpp)
list(APPEND SENCHA_GAME_COMPONENT_HEADERS
    samples/turret/TurretMount.h)
