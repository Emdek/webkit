/*
 * Copyright (C) 2019-2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"

#import "PlatformUtilities.h"
#import "TestWKWebView.h"
#import <WebKit/WKPreferencesPrivate.h>
#import <WebKit/WKWebViewConfiguration.h>
#import <WebKit/WKWebViewPrivate.h>

TEST(WKWebViewSuspendAllMediaPlayback, BeforeLoading)
{
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    configuration.get().mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
#if TARGET_OS_IPHONE
    configuration.get().allowsInlineMediaPlayback = YES;
#endif
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:CGRectMake(0, 0, 100, 100) configuration:configuration.get() addToWindow:YES]);
    [webView _suspendAllMediaPlayback];

    __block bool notPlaying = false;
    [webView performAfterReceivingMessage:@"not playing" action:^{ notPlaying = true; }];
    [webView synchronouslyLoadTestPageNamed:@"video-with-audio"];
    TestWebKitAPI::Util::run(&notPlaying);
}


TEST(WKWebViewSuspendAllMediaPlayback, AfterLoading)
{
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    configuration.get().mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
#if TARGET_OS_IPHONE
    configuration.get().allowsInlineMediaPlayback = YES;
#endif
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:CGRectMake(0, 0, 100, 100) configuration:configuration.get() addToWindow:YES]);

    __block bool isPlaying = false;
    [webView performAfterReceivingMessage:@"playing" action:^{ isPlaying = true; }];

    [webView synchronouslyLoadTestPageNamed:@"video-with-audio"];

    TestWebKitAPI::Util::run(&isPlaying);

    __block bool isPaused = false;
    [webView performAfterReceivingMessage:@"paused" action:^{ isPaused = true; }];
    [webView stringByEvaluatingJavaScript:@"document.querySelector('video').addEventListener('pause', paused);"];
    [webView _suspendAllMediaPlayback];

    TestWebKitAPI::Util::run(&isPaused);

    isPlaying = false;
    [webView performAfterReceivingMessage:@"playing" action:^{ isPlaying = true; }];
    [webView stringByEvaluatingJavaScript:@"document.querySelector('video').addEventListener('playing', playing);"];
    [webView _resumeAllMediaPlayback];

    TestWebKitAPI::Util::run(&isPlaying);
}

TEST(WKWebViewSuspendAllMediaPlayback, PauseWhenResume)
{
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    configuration.get().mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
#if TARGET_OS_IPHONE
    configuration.get().allowsInlineMediaPlayback = YES;
#endif
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:CGRectMake(0, 0, 100, 100) configuration:configuration.get() addToWindow:YES]);

    [webView synchronouslyLoadTestPageNamed:@"video-with-audio"];

    __block bool completionHandlerCalled = false;
    auto completionHandler = ^{
        completionHandlerCalled = true;
    };

    [webView suspendAllMediaPlayback:completionHandler];
    TestWebKitAPI::Util::run(&completionHandlerCalled);

    completionHandlerCalled = false;
    [webView pauseAllMediaPlaybackWithCompletionHandler:completionHandler];
    TestWebKitAPI::Util::run(&completionHandlerCalled);

    completionHandlerCalled = false;
    [webView resumeAllMediaPlayback:completionHandler];
    TestWebKitAPI::Util::run(&completionHandlerCalled);

    EXPECT_TRUE([[webView objectByEvaluatingJavaScript:@"document.querySelector('video').paused"] boolValue]);

}

TEST(WKWebViewSuspendAllMediaPlayback, FullscreenWhileSuspended)
{
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration preferences] _setFullScreenEnabled:YES];

    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:CGRectMake(0, 0, 100, 100) configuration:configuration.get() addToWindow:YES]);

    [webView synchronouslyLoadTestPageNamed:@"video-with-audio"];

    __block bool completionHandlerCalled = false;
    auto completionHandler = ^{
        completionHandlerCalled = true;
    };

    [webView suspendAllMediaPlayback:completionHandler];
    TestWebKitAPI::Util::run(&completionHandlerCalled);

    NSError *error = nil;
    EXPECT_NULL([webView objectByCallingAsyncFunction:@"return document.getElementsByTagName('video')[0].webkitEnterFullscreen()" withArguments:@{ } error:&error]);
    EXPECT_NULL(error);
    EXPECT_FALSE([[webView objectByEvaluatingJavaScript:@"document.webkitIsFullScreen"] boolValue]);
}
