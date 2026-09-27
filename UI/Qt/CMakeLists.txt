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
    ${RESOURCE_SOURCES}
)
set_target_properties(ladybird PROPERTIES OUTPUT_NAME ladybird-qt)

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

# The helper processes and resources come from the package's install prefix, where LibWebView looks
# for them relative to this executable (libexec/ and share/Lagom/).
install(TARGETS ladybird RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
