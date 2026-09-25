if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Configured on its own: build only the frontend, against the engine libraries and
    # headers of an installed Lagom package (ENABLE_INSTALL_HEADERS), which also carries
    # the compile options the headers depend on.
    cmake_minimum_required(VERSION 3.25)
    project(ladybird-gtk LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    include(GNUInstallDirs)
    find_package(Lagom CONFIG REQUIRED)

    set(LADYBIRD_GTK_STANDALONE ON)
    get_filename_component(LADYBIRD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

find_package(PkgConfig REQUIRED)
# GdkMemoryTextureBuilder is GTK 4.16, AdwAlertDialog and AdwDialog are libadwaita 1.5.
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.16)
pkg_check_modules(LIBADWAITA REQUIRED IMPORTED_TARGET libadwaita-1>=1.5)

add_executable(ladybird main.cpp)

find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources REQUIRED)

set(GTK_UI_RESOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/browser-window.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/list-popover.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/location-entry.ui"
)

set(GTK_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/Resources/resources.gresource.xml")
set(GTK_GRESOURCE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/gtk-ui-resources.cpp")

add_custom_command(
    OUTPUT "${GTK_GRESOURCE_SOURCE}"
    DEPENDS "${GTK_GRESOURCE_XML}" ${GTK_UI_RESOURCES}
    COMMAND "${GLIB_COMPILE_RESOURCES}"
        "${GTK_GRESOURCE_XML}"
        --sourcedir "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
        --target "${GTK_GRESOURCE_SOURCE}"
        --generate-source
    VERBATIM
)
add_custom_target(gtk_ui_resources DEPENDS "${GTK_GRESOURCE_SOURCE}")

target_sources(ladybird PRIVATE
    Application.cpp
    BrowserWindow.cpp
    Dialogs.cpp
    Events.cpp
    EventLoopImplementationGtk.cpp
    Menu.cpp
    Tab.cpp
    WebContentView.cpp
    Widgets/LadybirdBrowserWindow.cpp
    Widgets/LadybirdLocationEntry.cpp
    Widgets/LadybirdWebView.cpp
    "${GTK_GRESOURCE_SOURCE}"
)
target_link_libraries(ladybird PRIVATE PkgConfig::GTK4 PkgConfig::LIBADWAITA)
add_dependencies(ladybird gtk_ui_resources)

if (NOT LADYBIRD_GTK_STANDALONE)
    create_ladybird_bundle(ladybird)
    return()
endif()

# What UI/CMakeLists.txt does for an in-tree frontend, with the installed libraries.
# The helper processes and resources come from the package's install prefix, where
# LibWebView looks for them relative to this executable (libexec/ and share/Lagom/).
set_target_properties(ladybird PROPERTIES OUTPUT_NAME ladybird-gtk)

# The sources include each other as <UI/Gtk/...>. The source root cannot be the include
# directory for that: its AK/ and Libraries/ would shadow the installed headers, which
# are the ones the libraries were built from. Expose only this directory, as UI/Gtk.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/UI")
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/include/UI/Gtk" SYMBOLIC)
target_include_directories(ladybird PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")

target_link_libraries(ladybird PRIVATE
    Lagom::AK Lagom::Core Lagom::FileSystem Lagom::Gfx Lagom::ImageDecoderClient Lagom::IPC
    Lagom::JS Lagom::Main Lagom::Web Lagom::WebView Lagom::Requests Lagom::URL
    OpenSSL::Crypto OpenSSL::SSL)

install(TARGETS ladybird RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/org.ladybird.Ladybird.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${LADYBIRD_SOURCE_DIR}/Meta/CMake/freedesktop/org.ladybird.Ladybird.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
