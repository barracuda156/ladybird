# The Qt frontend, ported to Qt 4. It is built on its own, against the engine libraries and headers
# of an installed Lagom package (ENABLE_INSTALL_HEADERS), which also carries the compile options the
# headers depend on; the top-level build only knows Qt 6.
if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "The Qt frontend uses Qt 4 and is configured on its own, with UI/Qt as the source directory, against an installed Lagom package")
endif()

cmake_minimum_required(VERSION 3.25)
project(ladybird-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
find_package(Lagom CONFIG REQUIRED)

# CMake no longer ships FindQt4; cmake/ has its last version with the modules it uses.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Qt4 4.8 REQUIRED QtCore QtGui)

set(MOC_HEADERS
    Autocomplete.h
    BookmarksBar.h
    BrowserWindow.h
    EventLoopImplementationQtEventTarget.h
    FindInPageWidget.h
    LocationEdit.h
    Settings.h
    Tab.h
    TabBar.h
    WebContentView.h
)
qt4_wrap_cpp(MOC_SOURCES ${MOC_HEADERS})

# Compile the moc output as one translation unit, as upstream's AUTOMOC does: the headers rely on each
# other for complete types (TabWidget::tab() in TabBar.h casts to Tab, which Tab.h defines).
set(moc_includes "")
foreach (moc_source IN LISTS MOC_SOURCES)
    string(APPEND moc_includes "#include \"${moc_source}\"\n")
endforeach()
set(MOCS_COMPILATION "${CMAKE_CURRENT_BINARY_DIR}/mocs_compilation.cxx")
file(CONFIGURE OUTPUT "${MOCS_COMPILATION}" CONTENT "${moc_includes}" @ONLY)
set_source_files_properties(${MOC_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties("${MOCS_COMPILATION}" PROPERTIES OBJECT_DEPENDS "${MOC_SOURCES}")

# The resources are the icons in ../Icons.
qt4_add_resources(RESOURCE_SOURCES ladybird.qrc)

add_executable(ladybird
    main.cpp
    Application.cpp
    Autocomplete.cpp
    BookmarksBar.cpp
    BrowserWindow.cpp
    EventLoopImplementationQt.cpp
    EventLoopImplementationQtEventTarget.cpp
    FindInPageWidget.cpp
    Icon.cpp
    LocationEdit.cpp
    Menu.cpp
    Qt4Compat.cpp
    Settings.cpp
    StringUtils.cpp
    Tab.cpp
    TabBar.cpp
    TVGIconEngine.cpp
    WebContentView.cpp
    ${MOC_SOURCES}
    "${MOCS_COMPILATION}"
    ${RESOURCE_SOURCES}
)

# The sources include each other as <UI/Qt/...>. The source root cannot be the include directory for
# that: its AK/ and Libraries/ would shadow the installed headers, which are the ones the libraries
# were built from. Expose only this directory, as UI/Qt.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/UI")
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/include/UI/Qt" SYMBOLIC)
target_include_directories(ladybird PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")

# Qt 4 writes key combinations as Qt::CTRL | Qt::Key_X, an operation on two enumerations that C++20
# deprecated.
target_compile_options(ladybird PRIVATE -Wno-deprecated-enum-enum-conversion)
if (APPLE)
    # Should a system header bring in AssertMacros.h, keep it from defining lowercase check() and
    # verify() macros, which would clash with the engine's names.
    target_compile_definitions(ladybird PRIVATE __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0)
endif()

target_link_libraries(ladybird PRIVATE
    Qt4::QtGui Qt4::QtCore
    Lagom::AK Lagom::Core Lagom::FileSystem Lagom::Gfx Lagom::ImageDecoderClient Lagom::IPC
    Lagom::JS Lagom::Main Lagom::Web Lagom::WebView Lagom::Requests Lagom::URL
    OpenSSL::Crypto OpenSSL::SSL)

# The helper processes and resources are those of the installed Lagom package, in libexec/ and
# share/Lagom/ of its prefix. LibWebView finds them from the directory of the engine's binaries,
# which the browser passes on, as it may itself run from an application bundle.
set(LADYBIRD_ENGINE_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "Install prefix of the Lagom package whose helper processes and resources the browser uses")
target_compile_definitions(ladybird PRIVATE LADYBIRD_ENGINE_BINARY_DIRECTORY="${LADYBIRD_ENGINE_PREFIX}/${CMAKE_INSTALL_BINDIR}")

if (APPLE)
    # An application bundle, as upstream builds it: Ladybird.app, with the executable Ladybird that
    # Info.plist names.
    set_target_properties(ladybird PROPERTIES
        OUTPUT_NAME Ladybird
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../Info.plist"
    )

    # Upstream makes the icon with iconutil, which Mac OS X has from 10.7 on.
    find_program(MAKEICNS_EXECUTABLE makeicns)
    if (MAKEICNS_EXECUTABLE)
        set(iconset "${CMAKE_CURRENT_SOURCE_DIR}/../Icons/macos/app_icon.iconset")
        add_custom_command(TARGET ladybird POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ladybird>/Resources"
            COMMAND "${MAKEICNS_EXECUTABLE}"
                -512 "${iconset}/icon_512x512.png"
                -256 "${iconset}/icon_256x256.png"
                -128 "${iconset}/icon_128x128.png"
                -32 "${iconset}/icon_32x32.png"
                -16 "${iconset}/icon_16x16.png"
                -out "$<TARGET_BUNDLE_CONTENT_DIR:ladybird>/Resources/app_icon.icns"
            VERBATIM
        )
    else()
        message(WARNING "makeicns not found: Ladybird.app will have no icon")
    endif()

    set(LADYBIRD_APPLICATIONS_DIR "Applications" CACHE PATH "Where Ladybird.app is installed, relative to the prefix or absolute")
    install(TARGETS ladybird BUNDLE DESTINATION "${LADYBIRD_APPLICATIONS_DIR}")
else()
    set_target_properties(ladybird PROPERTIES OUTPUT_NAME ladybird-qt)
    install(TARGETS ladybird RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
