# A toolkit-free frontend: the LibWebView application driving the engine in one of its headless
# modes (--headless=screenshot|layout-tree|text|manual) or under WebDriver. It links the same
# libraries as the Qt and AppKit frontends but opens no window, so it builds wherever the engine
# and its helper processes build.
add_executable(ladybird main.cpp)

# WebDriver and the helper-process lookup expect the browser binary to be called "Ladybird".
set_target_properties(ladybird PROPERTIES OUTPUT_NAME "Ladybird")

# Resources are laid out like a Unix install (share/Lagom) on every platform, including macOS,
# since there is no application bundle to put them in.
copy_resources_to_build("${CMAKE_BINARY_DIR}/${IN_BUILD_PREFIX}${CMAKE_INSTALL_DATADIR}/Lagom" ladybird)
