set(SOURCES
    Cache/CacheEntry.cpp
    Cache/CacheIndex.cpp
    Cache/DiskCache.cpp
    Cache/DiskCacheSettings.cpp
    Cache/MemoryCache.cpp
    Cache/Utilities.cpp
    Cookie/Cookie.cpp
    Cookie/ParsedCookie.cpp
    HSTS/ParsedHSTSPolicy.cpp
    Header.cpp
    HeaderList.cpp
    HTTP.cpp
    HttpRequest.cpp
    Method.cpp
    NetworkIsolationKey.cpp
    Port.cpp
    Proxy.cpp
    StructuredFields.cpp
)

include(hsts_preload)
list(APPEND SOURCES ${HSTS_PRELOAD_SOURCES})

ladybird_lib(LibHTTP http)
import_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libhttp_rust FFI_HEADER RustFFI.h)
target_link_libraries(LibHTTP PRIVATE libhttp_rust LibCompress LibCore LibCrypto LibDatabase LibFileSystem LibIPC LibRegex LibTextCodec LibTLS LibUnicode LibURL)
ladybird_hide_rust_crate_symbols(LibHTTP libhttp_rust)
test_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libhttp_rust)
