include(fontconfig)

set(SOURCES
    Application.cpp
    Attribute.cpp
    Autocomplete.cpp
    BookmarkStore.cpp
    BrowserProcess.cpp
    ConsoleOutput.cpp
    CookieJar.cpp
    DOMNodeProperties.cpp
    FileDownloader.cpp
    HeadlessWebView.cpp
    HistoryStore.cpp
    HelperProcess.cpp
    Menu.cpp
    Mutation.cpp
    Plugins/ImageCodecPlugin.cpp
    Process.cpp
    ProcessHandle.cpp
    ProcessManager.cpp
    SearchEngine.cpp
    Settings.cpp
    SiteIsolation.cpp
    SourceHighlighter.cpp
    StorageJar.cpp
    URL.cpp
    UserAgent.cpp
    Utilities.cpp
    ViewImplementation.cpp
    WebContentClient.cpp
    WebUI.cpp
    WebUI/BookmarksUI.cpp
    WebUI/HistoryUI.cpp
    WebUI/ProcessesUI.cpp
    WebUI/SettingsUI.cpp
)

if (APPLE)
    list(APPEND SOURCES MachPortServer.cpp)
endif()

set(GENERATED_SOURCES ${CURRENT_LIB_GENERATED})

embed_as_string(
    "NativeStyleSheetSource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Native.css"
    "NativeStyleSheetSource.cpp"
    "native_stylesheet_source"
    NAMESPACE "WebView"
)

compile_ipc(UIProcessServer.ipc UIProcessServerEndpoint.h)
compile_ipc(UIProcessClient.ipc UIProcessClientEndpoint.h)

if (NOT CMAKE_INSTALL_LIBEXECDIR STREQUAL "libexec")
    set_source_files_properties(Utilities.cpp PROPERTIES COMPILE_DEFINITIONS LADYBIRD_LIBEXECDIR="${CMAKE_INSTALL_LIBEXECDIR}")
endif()

set(GENERATED_SOURCES
    ${GENERATED_SOURCES}
    ../../Services/RequestServer/RequestClientEndpoint.h
    ../../Services/RequestServer/RequestServerEndpoint.h
    ../../Services/WebContent/WebContentClientEndpoint.h
    ../../Services/WebContent/WebContentServerEndpoint.h
    ../../Services/WebContent/WebDriverClientEndpoint.h
    ../../Services/WebContent/WebDriverServerEndpoint.h
    ../../Services/WebContent/WebUIClientEndpoint.h
    ../../Services/WebContent/WebUIServerEndpoint.h
    NativeStyleSheetSource.cpp
    UIProcessClientEndpoint.h
    UIProcessServerEndpoint.h
)

ladybird_lib(LibWebView webview EXPLICIT_SYMBOL_EXPORT)
target_link_libraries(LibWebView PRIVATE LibCore LibDatabase LibDevTools LibFileSystem LibGfx LibHTTP LibImageDecoderClient LibIPC LibRequests LibJS LibWeb LibUnicode LibURL LibSyntax LibTextCodec)

if (APPLE)
    target_link_libraries(LibWebView PRIVATE LibThreading)
endif()

# Third-party
if (HAS_FONTCONFIG)
    target_link_libraries(LibWebView PRIVATE Fontconfig::Fontconfig)
endif()
