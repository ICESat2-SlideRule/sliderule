message (STATUS "Including ccsds package")

target_compile_definitions (slideruleLib PUBLIC __ccsds__)

target_sources (slideruleLib
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/ccsds.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacket.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacketizer.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacketInterleaver.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacketParser.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserAOSFrameModule.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserModule.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserStripModule.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserZFrameModule.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPayloadDispatch.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsRecord.cpp
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsRecordDispatcher.cpp
)

target_include_directories (slideruleLib
    PUBLIC
        $<INSTALL_INTERFACE:${INCDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
)

install (
    FILES
        ${CMAKE_CURRENT_LIST_DIR}/ccsds.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacket.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacketizer.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacketInterleaver.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPacketParser.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserAOSFrameModule.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserModule.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserStripModule.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsParserZFrameModule.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsPayloadDispatch.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsRecord.h
        ${CMAKE_CURRENT_LIST_DIR}/CcsdsRecordDispatcher.h
    DESTINATION
        ${INCDIR}
)
