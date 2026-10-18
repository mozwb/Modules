# Tools and tests in this directory are built as part of the X_Y source tree.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ImageTool.cpp")
    add_executable(XYImageTool
        src/ImageTool.cpp)
    target_link_libraries(XYImageTool PRIVATE UI)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/MediaTool.cpp")
    add_executable(XYMediaTool
        src/MediaTool.cpp)
    target_link_libraries(XYMediaTool PRIVATE XCore)
endif()

add_executable(XYAudioOutputTool
    src/AudioOutputTool.cpp)
target_link_libraries(XYAudioOutputTool PRIVATE Media)

add_executable(XYDirectoryTreeTool
    src/DirectoryTreeTool.cpp)
target_link_libraries(XYDirectoryTreeTool PRIVATE XCore)

add_executable(XYNetworkSelfTest
    src/NetworkSelfTest.cpp)
find_package(Threads REQUIRED)
target_link_libraries(XYNetworkSelfTest PRIVATE Network Threads::Threads)

add_executable(XYVisualTest
    src/main.cpp)
target_link_libraries(XYVisualTest PRIVATE UI)

if(BUILD_TESTING)
    if(TARGET XYImageTool)
        add_test(NAME XYImageDecoderSelfTest COMMAND XYImageTool --self-test)
    endif()
    if(TARGET XYMediaTool)
        add_test(NAME XYMediaDecoderSelfTest COMMAND XYMediaTool --self-test)
    endif()
    add_test(NAME XYMediaPlayerSelfTest COMMAND XYAudioOutputTool --self-test)
    add_test(NAME XYNetworkSelfTest COMMAND XYNetworkSelfTest)
    add_test(NAME XYDirectoryTreeSelfTest COMMAND XYDirectoryTreeTool --self-test)
endif()
