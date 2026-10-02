
set(TRS_IO TRS-IO/src/esp/components)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_definitions(-DHAVE_CONFIG_H)
endif()

#FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/TRS-IO/src/esp/components/*.*)

#idf_component_register(SRCS ${app_sources}
idf_component_register(SRC_DIRS ${TRS_IO}/libsmb2/lib
                                ${TRS_IO}/trs-io
                                ${TRS_IO}/frehd
                                ${TRS_IO}/retrostore
                                ${TRS_IO}/trs-fs
                                ${TRS_IO}/tcpip
                                ${TRS_IO}/retrostore-c-sdk/main
                                ${TRS_IO}/retrostore-c-sdk/main/proto
                       INCLUDE_DIRS ${TRS_IO}/libsmb2/include
                                    ${TRS_IO}/libsmb2/include/smb2
                                    ${TRS_IO}/libsmb2/lib
                                    ${TRS_IO}/libsmb2/include/esp
                                    ${TRS_IO}/trs-io/include
                                    ${TRS_IO}/frehd/include
                                    ${TRS_IO}/retrostore/include
                                    ${TRS_IO}/trs-fs/include
                                    ${TRS_IO}/tcpip/include
                                    ${TRS_IO}/retrostore-c-sdk/main/include
                                    ${TRS_IO}/retrostore-c-sdk/main/proto
                        EMBED_TXTFILES ${TRS_IO}/trs-io/index.html
                                      ${TRS_IO}/trs-io/printer.html
                                      ${TRS_IO}/trs-io/AnotherMansTreasureMIII64C.ttf
                       EMBED_FILES ${TRS_IO}/retrostore/loader_basic.cmd
                                   ${TRS_IO}/retrostore/loader_cmd.bin
                                   ${TRS_IO}/retrostore/rsclient.cmd
                                   ${TRS_IO}/trs-io/xray-load.bin
                       REQUIRES main
                                esp_http_client
                                fatfs
                                esp_driver_uart
                                esp_driver_gpio
                                esp_driver_spi
                                esp_driver_sdmmc
                                esp_driver_sdspi)

# The vendored retrostore-c-sdk hands a malloc()ed buffer to a
# std::unique_ptr (retrostore.cpp, pb_MediaImageData_callback), which GCC
# only notices once inlining kicks in at -O2. Keep it a warning here rather
# than patching the submodule; the proper fix belongs upstream.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=mismatched-new-delete)

# Force static linking of modules
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u theTrsIOCoreModule")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u init_frehd")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u frehd_in")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u frehd_out")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u theTrsFileSystemModule")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u theRetroStoreModule")
set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-u theTCPIPModule")
