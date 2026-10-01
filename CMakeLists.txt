# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(bandsight LANGUAGES CXX C VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)


find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(LIBBPF REQUIRED IMPORTED_TARGET libbpf)

# Sources are appended by later tasks.
add_library(bandsight_core STATIC
    daemon/time_buckets.cpp
    daemon/iface_class.cpp
    daemon/app_name.cpp
    daemon/config_policy.cpp
    daemon/netlink_stats.cpp
    daemon/store.cpp
    daemon/rollup.cpp
    daemon/daemon_loop.cpp
    daemon/resolver.cpp
    daemon/collector.cpp
    daemon/json_frame.cpp
    daemon/unix_socket_control.cpp
)
target_include_directories(bandsight_core PUBLIC ${CMAKE_SOURCE_DIR}/daemon)
target_link_libraries(bandsight_core PUBLIC PkgConfig::SQLITE3 PkgConfig::LIBBPF)

add_executable(bandsightd daemon/main.cpp)
target_link_libraries(bandsightd PRIVATE bandsight_core)

include(GNUInstallDirs)

# Substitutes @BANDSIGHT_BINDIR@ in TEMPLATE and installs the result as NAME
# under DEST, doing both at install time so the path reflects the prefix the
# install is actually using rather than the one configure saw. DEST may be
# absolute (systemd's directory is not under the prefix) or relative to it.
function(bandsight_install_configured TEMPLATE NAME DEST)
  install(CODE "
    set(BANDSIGHT_BINDIR \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")

    # A relative destination would resolve against the working directory rather
    # than the prefix, which wrote share/applications into the source tree.
    set(_dest \"${DEST}\")
    if(NOT IS_ABSOLUTE \"\${_dest}\")
      set(_dest \"\${CMAKE_INSTALL_PREFIX}/\${_dest}\")
    endif()
    set(_dest \"\$ENV{DESTDIR}\${_dest}\")

    # Substituted straight into place, with no copy staged in the build tree.
    # Staging there meant a `sudo cmake --install` left root-owned files in a
    # user's build directory, after which every later non-root install died with
    # 'configure_file: Operation not permitted' - the build tree is the user's,
    # and an install must not write to it.
    file(MAKE_DIRECTORY \"\${_dest}\")
    configure_file(\"${CMAKE_SOURCE_DIR}/${TEMPLATE}\" \"\${_dest}/${NAME}\" @ONLY)
    message(STATUS \"Installing: \${_dest}/${NAME}\")
  ")
endfunction()

install(TARGETS bandsightd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ExecStart must point wherever the binary actually lands, so the unit is
# generated from the install prefix rather than hardcoding /usr/bin. Installing
# with the default /usr/local prefix previously produced a unit that crash-looped
# on 203/EXEC because the two paths disagreed.
#
# It is generated at INSTALL time, not configure time, and that distinction is
# the whole point. configure_file() would bake in whatever CMAKE_INSTALL_PREFIX
# was when cmake last ran, so the documented `cmake --install build --prefix
# /usr` put the binary in /usr/bin and left the unit pointing at
# /usr/local/bin - reintroducing the exact 203/EXEC loop this comment describes,
# while the README claimed it could not happen. Inside install(CODE),
# CMAKE_INSTALL_PREFIX is the prefix actually being installed to.
bandsight_install_configured(packaging/bandsightd.service.in
                             bandsightd.service
                             /usr/lib/systemd/system)

include(cmake/BuildBPF.cmake)
bandsight_add_bpf(bandsight_bpf bpf/bandsight.bpf.c)

# bandsight_core includes the generated skeleton (bandsight.skel.h) via
# collector.cpp, so it must be built after bandsight_bpf generates it, and
# needs the build directory on its include path to find it.
target_include_directories(bandsight_core PUBLIC ${CMAKE_BINARY_DIR})
add_dependencies(bandsight_core bandsight_bpf)

# ---- GUI -------------------------------------------------------------------
option(BANDSIGHT_BUILD_GUI "Build the Qt6 GUI" ON)
if(BANDSIGHT_BUILD_GUI)
  find_package(Qt6 6.4 QUIET COMPONENTS Widgets Charts Sql Network)
  if(NOT Qt6_FOUND)
    message(STATUS "bandsight: Qt6 Widgets/Charts/Sql not found - GUI disabled, daemon-only build")
    set(BANDSIGHT_BUILD_GUI OFF)
  endif()
endif()

if(BANDSIGHT_BUILD_GUI)
  add_executable(bandsight
      gui/main.cpp
      gui/frame_codec.cpp
      gui/units.cpp
      gui/app_labels.cpp
      gui/smooth.cpp
      gui/quota.cpp
      gui/quota_bar.cpp
      gui/settings_dialog.cpp
      gui/failure.cpp
      gui/live_client.cpp
      gui/history_store.cpp
      gui/live_ring.cpp
      gui/models/iface_model.cpp
      gui/tabs/graph_tab.cpp
      gui/tabs/iface_tab.cpp
      gui/tabs/usage_tab.cpp
      gui/theme.cpp
      gui/widgets/card.cpp
      gui/widgets/donut_chart.cpp
      gui/widgets/identity_chip.cpp
      gui/widgets/ranked_list.cpp
      gui/widgets/stat_pair.cpp
      gui/widgets/timeline_strip.cpp
      gui/widgets/totals_card.cpp
      gui/widgets/traffic_chart.cpp
      gui/tray_icon.cpp
      gui/main_window.cpp
      gui/resources/bandsight.qrc
      # HistoryStore builds its zero-fill grid with the daemon's bucket_start /
      # bucket_next so the two agree on where a bucket begins. This one file is
      # compiled in directly rather than linking bandsight_core, which would drag
      # sqlite, libbpf and the BPF skeleton into the GUI for two functions whose
      # only dependency is <ctime>. The test target gets them from bandsight_core.
      daemon/time_buckets.cpp
  )
  target_include_directories(bandsight PRIVATE ${CMAKE_SOURCE_DIR}/gui ${CMAKE_SOURCE_DIR}/daemon)
  target_link_libraries(bandsight PRIVATE Qt6::Widgets Qt6::Charts Qt6::Sql Qt6::Network)
  install(TARGETS bandsight RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  # Exec must point wherever the binary actually lands, for the same reason the
  # systemd unit is generated rather than hardcoded: a default /usr/local build
  # plus a hardcoded /usr/bin Exec is a launcher entry that silently does
  # nothing. Autostart is deliberately NOT installed - spec §4.7 makes the GUI a
  # viewer, and monitoring does not depend on it, so starting at login is a
  # setting the user opts into rather than a package default.
  bandsight_install_configured(packaging/bandsight.desktop.in
                               bandsight.desktop
                               ${CMAKE_INSTALL_DATADIR}/applications)
  set_target_properties(bandsight PROPERTIES AUTOMOC ON AUTORCC ON)

  # The app icon, pre-rendered from docs/branding/bandsight-icon-dark.png (the
  # dark tile: the transparent light variant is a navy outline that vanishes on
  # a dark panel). Named "bandsight" in hicolor so the launcher entry, the
  # Wayland window (matched via the desktop file name) and the tray all resolve
  # it from the theme; the qrc copy covers running from the build tree.
  foreach(size 16 22 24 32 48 64 128 256 512)
    install(FILES packaging/icons/hicolor/${size}x${size}/apps/bandsight.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
  endforeach()

endif()

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/bandsight)

# ---- Packaging (cpack -G DEB / cpack -G RPM) ---------------------------------
set(CPACK_PACKAGE_NAME "bandsight")
set(CPACK_PACKAGE_VENDOR "Isuru Ranaweera")
set(CPACK_PACKAGE_CONTACT "Isuru Ranaweera <isururanaweera20@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Per-application network usage monitor for Linux (eBPF daemon + Qt6 GUI)")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Isuru-rana/bandsight")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_SOURCE_DIR}/packaging/deb/prerm;${CMAKE_SOURCE_DIR}/packaging/deb/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase-sqlite or libQt6Sql6-sqlite")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/post.sh")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/preun.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/postun.sh")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/lib/systemd /usr/lib/systemd/system /usr/share/applications
    /usr/share/icons /usr/share/icons/hicolor /usr/share/licenses)

include(CPack)
