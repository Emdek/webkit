# Copyright (C) 2010 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the Google name nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""QtWebKit implementation of the Port interface."""

import os

import glob
import logging
import re
import sys
import platform

from webkitpy.common.memoized import memoized
from webkitpy.common.system import path
from webkitpy.layout_tests.models.test_configuration import TestConfiguration
from webkitpy.port.base import Port
from webkitpy.port.xorgdriver import XorgDriver
from webkitpy.port.xvfbdriver import XvfbDriver
from webkitpy.port.linux_get_crash_log import GDBCrashLogGenerator
from webkitcorepy import decorators

_log = logging.getLogger(__name__)


class QtPort(Port):
    ALL_VERSIONS = ['linux', 'win', 'mac']
    port_name = "qt"

    def _wk2_port_name(self):
        return "qt-wk2"

    def _port_flag_for_scripts(self):
        return "--qt"

    @classmethod
    def determine_full_port_name(cls, host, options, port_name):
        if port_name and port_name != cls.port_name:
            return port_name
        return port_name + '-' + host.platform.os_name

    # sys_platform exists only for unit testing.
    def __init__(self, host, port_name, **kwargs):
        super(QtPort, self).__init__(host, port_name, **kwargs)

        self._operating_system = port_name.replace('qt-', '')

        # FIXME: Why is this being set at all?
        self._version = self.operating_system()

        if self._should_use_jhbuild():
            self._jhbuild_wrapper = [self.path_from_webkit_base('Tools', 'jhbuild', 'jhbuild-wrapper'), '--qt', 'run']
            if self.get_option('wrapper'):
                self.set_option('wrapper', ' '.join(self._jhbuild_wrapper) + ' ' + self.get_option('wrapper'))
            else:
                self.set_option_default('wrapper', ' '.join(self._jhbuild_wrapper))

    def _generate_all_test_configurations(self):
        configurations = []
        for version in self.ALL_VERSIONS:
            for build_type in self.ALL_BUILD_TYPES:
                configurations.append(TestConfiguration(version=version, architecture='x86', build_type=build_type))
        return configurations

    def _build_driver(self):
        # The Qt port builds DRT as part of the main build step
        return True

    def supports_per_test_timeout(self):
        return True

    def _driver_class(self):
        if os.environ.get("USE_NATIVE_XDISPLAY"):
            return XorgDriver
        return XvfbDriver

    def _path_to_driver(self):
        return self._build_path('bin/%s' % self.driver_name())

    @decorators.Memoize()
    def _path_to_image_diff(self):
        return self._build_path('bin/ImageDiff')

    def _path_to_webcore_library(self):
        if self.operating_system() == 'mac':
            return self._build_path('lib/QtWebKitWidgets.framework/QtWebKitWidgets')
        else:
            return self._build_path('lib/libQt5WebKitWidgets.so')

    def _modules_to_search_for_symbols(self):
        # We search in every library to be reliable in the case of building with CONFIG+=force_static_libs_as_shared.
        if self.operating_system() == 'mac':
            frameworks = glob.glob(os.path.join(self._build_path('lib'), '*.framework'))
            return [os.path.join(framework, os.path.splitext(os.path.basename(framework))[0]) for framework in frameworks]
        else:
            suffix = 'dll' if self.operating_system() == 'win' else 'so'
            return glob.glob(os.path.join(self._build_path('lib'), 'lib*.' + suffix))

    @memoized
    def qt_version(self):
        version = ''
        try:
            for line in self.host.executive.run_command(self._jhbuild_wrapper + ['qmake', '-v']).split('\n'):
                match = re.search('Qt\sversion\s(?P<version>\d\.\d)', line)
                if match:
                    version = match.group('version')
                    break
        except OSError:
            version = '5.0'
        return version

    def _search_paths(self):
        #                 qt-mac-wk2
        #                /
        #       qt-wk1  qt-wk2
        #             \/
        #           qt-5.x
        #               \
        #    (qt-linux|qt-mac|qt-win)
        #                |
        #               qt
        search_paths = []
        if self.get_option('webkit_test_runner'):
            if self.operating_system() == 'mac':
                search_paths.append('qt-mac-wk2')
            search_paths.append('qt-wk2')
        else:
            search_paths.append('qt-wk1')

        search_paths.append('qt-' + self.qt_version())

        search_paths.append(self.port_name + '-' + self.operating_system())
        search_paths.append(self.port_name)
        return search_paths

    def default_baseline_search_path(self, **kwargs):
        return list(map(self._webkit_baseline_path, self._search_paths()))

    def _port_specific_expectations_files(self, **kwargs):
        paths = self._search_paths()
        if self.get_option('webkit_test_runner'):
            paths.append('wk2')

        # expectations_files() uses the directories listed in _search_paths reversed.
        # e.g. qt -> qt-linux -> qt-5.x -> qt-wk1
        return list(reversed([self._filesystem.join(self._webkit_baseline_path(p), 'TestExpectations') for p in paths]))

    def setup_environ_for_server(self, server_name=None):
        clean_env = super(QtPort, self).setup_environ_for_server(server_name)
        clean_env['QTWEBKIT_PLUGIN_PATH'] = self._build_path('lib')
        self._copy_value_from_environ_if_set(clean_env, 'QT_DRT_WEBVIEW_MODE')
        self._copy_value_from_environ_if_set(clean_env, 'DYLD_IMAGE_SUFFIX')
        self._copy_value_from_environ_if_set(clean_env, 'QT_WEBKIT_LOG')
        self._copy_value_from_environ_if_set(clean_env, 'DISABLE_NI_WARNING')
        self._copy_value_from_environ_if_set(clean_env, 'QT_WEBKIT_PAUSE_UI_PROCESS')
        self._copy_value_from_environ_if_set(clean_env, 'QT_QPA_PLATFORM_PLUGIN_PATH')
        self._copy_value_from_environ_if_set(clean_env, 'QT_WEBKIT_DISABLE_UIPROCESS_DUMPPIXELS')
        if self._driver_class() == XvfbDriver and self._should_use_jhbuild():
            llvmpipe_libgl_path = self.host.executive.run_command(self._jhbuild_wrapper + ['printenv', 'LLVMPIPE_LIBGL_PATH'],
                                                                  error_handler=self.host.executive.ignore_error).strip()
            if os.path.exists(os.path.join(llvmpipe_libgl_path, "libGL.so")):
                # Force the Gallium llvmpipe software rasterizer
                clean_env['LD_LIBRARY_PATH'] = llvmpipe_libgl_path
                if os.environ.get('LD_LIBRARY_PATH'):
                    clean_env['LD_LIBRARY_PATH'] += ':%s' % os.environ.get('LD_LIBRARY_PATH')
            else:
                _log.warning("Can't find Gallium llvmpipe driver. Try to run update-webkitgtk-libs")
        return clean_env

    # FIXME: We should find a way to share this implmentation with Gtk,
    # or teach run-launcher how to call run-safari and move this down to Port.
    def show_results_html_file(self, results_filename):
        run_launcher_args = [path.abspath_to_uri(self.host.platform, results_filename)]
        if self.get_option('webkit_test_runner'):
            run_launcher_args.append('-2')
        self._run_script("run-minibrowser", run_launcher_args)

    def operating_system(self):
        return self._operating_system

    def check_sys_deps(self):
        result = super(QtPort, self).check_sys_deps()
        if not 'WEBKIT_TESTFONTS' in os.environ:
            _log.error('\nThe WEBKIT_TESTFONTS environment variable is not defined or not set properly.')
            _log.error('You must set it before running the tests.')
            _log.error('Use git to grab the actual fonts from https://github.com/carewolf/qtwebkit-testfonts.git')
            return False
        return result

    # Qt port is not ready for parallel testing, see https://bugs.webkit.org/show_bug.cgi?id=77730 for details.
    def default_child_processes(self, **kwargs):
        return 1

    def build_webkit_command(self, build_style=None):
        command = super(QtPort, self).build_webkit_command(build_style)
        command.append("--qt")
        if not self.get_option('webkit_test_runner'):
            command.append("--no-webkit2")
        command.append(super(QtPort, self).make_args())
        return command

    def run_webkit_tests_command(self):
        command = super(QtPort, self).run_webkit_tests_command()
        command.append("--qt")
        if self.get_option('webkit_test_runner'):
            command.append("-2")
        return command

    def _get_crash_log(self, name, pid, stdout, stderr, newer_than, target_host=None):
        return GDBCrashLogGenerator(self._executive, name, pid, newer_than,
                                    self._filesystem, self._path_to_driver, self.port_name, self.get_option('configuration')).generate_crash_log(stdout, stderr)
