include(CMakeFindDependencyMacro)
# Imported targets named in the exported link interfaces (LibMain links threads, LibCrypto and LibTLS
# link OpenSSL publicly).
find_dependency(Threads)
find_dependency(OpenSSL)
include("${CMAKE_CURRENT_LIST_DIR}/LagomTargets.cmake")
