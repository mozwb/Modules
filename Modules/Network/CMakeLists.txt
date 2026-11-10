# Network uses the platform socket implementation selected for this target.
if(WIN32)
    xy_module(Network STATIC DEPS Thread EXCLUDE "/src/Linux/")
    target_link_libraries(Network PUBLIC ws2_32)
else()
    xy_module(Network STATIC DEPS Thread EXCLUDE "/src/Windows/")
endif()
