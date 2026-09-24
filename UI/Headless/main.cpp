/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <LibMain/Main.h>
#include <LibWebView/Application.h>
#include <LibWebView/Options.h>

namespace Ladybird {

// A frontend without any windowing toolkit. It drives the engine in one of its headless modes
// (--headless=screenshot|layout-tree|text|manual) or under WebDriver, and builds anywhere the
// engine and its helper processes do.
class Application final : public WebView::Application {
    WEB_VIEW_APPLICATION(Application)

public:
    Application() = default;

private:
    virtual void create_platform_options(WebView::BrowserOptions& browser_options, WebView::RequestServerOptions&, WebView::WebContentOptions&) override
    {
        // There is no window to show, so a run without an explicit mode dumps the page text.
        if (!browser_options.headless_mode.has_value())
            browser_options.headless_mode = WebView::HeadlessMode::Text;
    }
};

}

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    AK::set_rich_debug_enabled(true);

    auto app = TRY(Ladybird::Application::create(arguments));
    return app->execute();
}
