/*
 * Copyright (c) 2025, Ladybird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Error.h>
#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <LibGC/Ptr.h>
#include <LibJS/Runtime/Object.h>
#include <LibURL/URL.h>

namespace Web::Extensions::API {

class ChromeWebStore final : public JS::Object {
    JS_OBJECT(ChromeWebStore, JS::Object);
    GC_DECLARE_ALLOCATOR(ChromeWebStore);

public:
    static GC::Ref<ChromeWebStore> create(JS::Realm& realm);
    virtual ~ChromeWebStore() override = default;

    virtual void initialize(JS::Realm& realm) override;

    // Chrome Web Store API methods
    JS_DECLARE_NATIVE_FUNCTION(install);
    JS_DECLARE_NATIVE_FUNCTION(enable);
    JS_DECLARE_NATIVE_FUNCTION(disable);

    // Event handlers
    void on_install_success(Function<void(String const& extension_id)> callback);
    void on_install_failure(Function<void(String const& error)> callback);

private:
    explicit ChromeWebStore(JS::Realm& realm);

    virtual void visit_edges(GC::Cell::Visitor&) override;

    // Internal installation methods
    ErrorOr<void> download_and_install_extension(String const& extension_id, String const& web_store_url);
    ErrorOr<Vector<u8>> download_crx_file(URL::URL const& download_url);
    ErrorOr<String> extract_and_install_crx(Vector<u8> const& crx_data, String const& extension_id);
    static Optional<String> extract_extension_id_from_webstore_url(StringView url);

    // Event callbacks
    Vector<Function<void(String const&)>> m_install_success_callbacks;
    Vector<Function<void(String const&)>> m_install_failure_callbacks;
};

} 