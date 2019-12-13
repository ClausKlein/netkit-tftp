# -*- coding: utf-8 -*-
"""Defines build steps of project."""

import shutil
import tempfile

from tools.buildctrl.agenthelper import *
from tools.buildctrl.config import Config
from tools.buildctrl.install import InstallCreator


def dependencies():
    # return list of applications, required to
    # build before this application is constructed
    return [asio]


def build(ctx:BuildContext):
    # full path to where ctrl-agent.py is stored,
    src_path = ctx.path_package

    # build header only lib
    for type in [Config.SDK_ENV_64BIT]:
        # setup separate build dir
        build_path = f"{ctx.path_build}/{type}"
        os.makedirs(build_path, exist_ok=True)

        env = sdk_environment_load(ctx, sdk_env=type)
        cmake_variables = cmake_variables_load(ctx, sdk_env=type)

        # Configuration of package via cmake
        cmake_execute(ctx, src_path=src_path, env=env, cwd=build_path,
                      cmake_variables=cmake_variables)

        # Actual build with ninja build tool
        ninja_build(ctx, env=env, cwd=build_path)

        # Actual install with ninja build tool
        ninja_install(ctx, env=env, cwd=build_path)


def clean(ctx: CleanContext):
    shutil.rmtree(ctx.path_build, ignore_errors=True)


def deploy(ctx:DeployContext):
    # source code directory
    src_path = ctx.path_package
    # only dev supported
    ctx.set_supported_modes([Config.PACKAGE_DEPLOYMENT_MODE_DEV])
    # install the applications, libraries, headers and cmake files using ninja
    # into a temporary location
    tmp = tempfile.TemporaryDirectory()

    # deploy header and cmake pakage config
    for type in [Config.SDK_ENV_64BIT]:
        # setup separate build dir
        build_path = f"{ctx.path_build}/{type}"

        env = sdk_environment_load(ctx, sdk_env=type)
        cmake_variables = cmake_variables_load(ctx, sdk_env=type)
        cmake_variables['CMAKE_INSTALL_PREFIX'] = tmp.name

        # Configuration of package via cmake
        # must be reconfigured since the configured CMAKE_INSTALL_PREFIX can't be altered only appended...
        # e.g. DESTDIR='/usr' ninja install -> /usr/${CMAKE_INSTALL_PREFIX}/installation/path
        cmake_execute(ctx, src_path=src_path, env=env, cwd=build_path,
                      cmake_variables=cmake_variables)

        # Actual install with ninja build tool
        ninja_install(ctx, env=env, cwd=build_path)

    # copy file from installed tmp location and/or build_path to deploy
    for mode in ctx.get_modes():
        installer = InstallCreator(cwd=tmp.name, target_dir=Path(ctx.path_deploy) / mode)
        if mode == Config.PACKAGE_DEPLOYMENT_MODE_DEV:
            # convert installed files to installable package
            installer.convert_dir_to_package(path=tmp.name, include_patterns=['/lib*', '/include*'], skip_empty=True)
            #XXX installer.convert_dir_to_package(path=tmp.name, include_patterns=['/cmake*'], skip_empty=True)
        elif mode == Config.PACKAGE_DEPLOYMENT_MODE_RUN:
            # install applications and libraries only
            pass
        elif mode == Config.PACKAGE_DEPLOYMENT_MODE_SRC:
            # copy sources into package
            pass
        installer.self_copy_to_target()


def test(ctx:TestContext):
    pass
