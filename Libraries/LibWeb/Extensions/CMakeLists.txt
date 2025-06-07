set(SOURCES
    Extension.cpp
    ExtensionManifest.cpp
    ExtensionManager.cpp
    API/ChromeWebStore.cpp
    API/ExtensionRuntime.cpp
    API/MozillaExtensionRuntime.cpp
)

serenity_lib(LibWebExtensions extensions)
target_link_libraries(LibWebExtensions PRIVATE LibCore LibCrypto LibFileSystem LibGC LibGfx LibIPC LibJS LibURL LibWeb) 