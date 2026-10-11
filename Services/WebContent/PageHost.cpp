/*
 * Copyright (c) 2020-2023, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021-2022, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <WebContent/ConnectionFromClient.h>
#include <WebContent/PageClient.h>
#include <WebContent/PageHost.h>
#include <WebContent/WebDriverConnection.h>

namespace WebContent {

PageHost::PageHost(ConnectionFromClient& client)
    : m_client(client)
{
    auto& first_page = create_page();
    Web::HTML::TraversableNavigable::create_a_fresh_top_level_traversable(first_page.page(), URL::about_blank()).release_value_but_fixme_should_propagate_errors();
}

PageClient& PageHost::create_page()
{
    m_pages.set(m_next_id, PageClient::create(Web::Bindings::main_thread_vm(), *this, m_next_id));
    ++m_next_id;
    return *m_pages.get(m_next_id - 1).value();
}

void PageHost::remove_page(Badge<PageClient>, u64 index)
{
    m_pages.remove(index);
}

void PageHost::close_page(u64 index)
{
    auto page = m_pages.get(index);
    if (!page.has_value())
        return;

    // NOTE: Dropping our reference alone would leave the page running (timers, loads and painting) until it is GC'd,
    //       so close its traversable first. This ends in PageClient::page_did_close_top_level_traversable(), which
    //       removes the page from m_pages.
    auto& page_client = *page.value();
    if (page_client.page().top_level_traversable_is_initialized())
        page_client.page().top_level_traversable()->close_top_level_traversable();
    else
        m_pages.remove(index);
}

Optional<PageClient&> PageHost::page(u64 index)
{
    return m_pages.get(index).map([](auto& value) -> PageClient& {
        return *value;
    });
}

PageHost::~PageHost() = default;

}
